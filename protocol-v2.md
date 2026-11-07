# Đặc tả giao thức ứng dụng Chat

Tài liệu mô tả toàn bộ cách các thành phần của hệ thống nói chuyện với nhau: khung dữ liệu trên TCP, mã hóa đường truyền, cấu trúc message, phiên làm việc, audit log mạng, lõi mạng C ở phía client, bot và cách đo. Chức năng của hệ thống nằm ở [chuc-nang.md](chuc-nang.md).

## 1. Kiến trúc

```
React (renderer) ──IPC──> Electron main ──pipe──> netchat-core (C) ──TCP──> Server C (epoll) ──> PostgreSQL
 giao diện               chuyển tiếp,              socket, khung,           nghiệp vụ
                         không có socket           mã hóa, heartbeat,
                                                   reconnect, audit
```

| Thành phần | Ngôn ngữ | Trách nhiệm |
|---|---|---|
| Renderer | React/TypeScript | Giao diện và trạng thái hiển thị. Không có quyền ra mạng (CSP `connect-src 'none'`). |
| Electron main | JavaScript | Khởi chạy `netchat-core`, chuyển tiếp IPC ↔ pipe, ghép `id` của request với `Promise`. Không mở socket. |
| **netchat-core** | **C** | `connect`, đọc/ghi socket, ghép/tách khung, `seq`/`ack`, mã hóa, `HELLO`, `PING`, kết nối lại + `RESUME`, đếm thống kê, audit log phía client. |
| **Server C** | **C** | `epoll` một thread, khung, giải mã, nghiệp vụ, phiên, định tuyến tin, rate limit, ghi DB, audit log phía server. |
| PostgreSQL | SQL | Lưu user, bạn bè, nhóm, tin nhắn, trạng thái giao. Chạy trong Docker. |
| Bot, bộ tạo tải | Python, C | Nhiều tài khoản tự động để demo và đo (mục 12). |

- Đường dữ liệu giữa client và server chỉ có **TCP thuần** tại cổng mặc định `9000`. Không có HTTP, không có WebSocket.
- Kiểm tra được: `grep -rE "node:net|fetch\(|WebSocket|XMLHttpRequest" client/src client/electron` không có kết quả.
- Server chạy trong WSL (Linux, vì dùng `epoll`); PostgreSQL chạy bằng Docker (`docker compose up -d db`, cổng 5432).

---

## 2. Khung dữ liệu trên TCP

TCP là luồng byte, không có ranh giới message, nên mỗi message được đóng khung:

```
+---------------+---------------+---------------+-------------------------------+
| length (4 B)  | seq (4 B)     | ack (4 B)     | payload (length byte)         |
| uint32 BE     | uint32 BE     | uint32 BE     | JSON UTF-8, đã mã hóa (mục 8) |
+---------------+---------------+---------------+-------------------------------+
```

| Trường | Ý nghĩa |
|---|---|
| `length` | Độ dài **chỉ của payload** (không tính 12 byte header). `1 ≤ length ≤ 65536`. |
| `seq` | Số thứ tự của khung này trong chiều gửi của kết nối. Khung đầu tiên mỗi bên gửi có `seq = 1`, mỗi khung sau tăng 1. Tính theo modulo 2³². |
| `ack` | `seq` lớn nhất mà bên gửi **đã nhận được** từ bên kia trên kết nối này (0 nếu chưa nhận khung nào). |

- `seq` và `ack` do tầng mạng (`net.c` ở server, `netchat-core` ở client) tự điền ở mọi khung, tầng nghiệp vụ không biết tới chúng. Mục đích: phát hiện ngay khung bị mất (mục 9).
- Header luôn ở dạng rõ; chỉ payload được mã hóa.
- Mọi số nguyên trong header là big-endian (`htonl`/`ntohl`).
- Người nhận phải tự ghép/tách:
  - Một lần `recv` có thể chứa **nhiều** khung liền nhau.
  - Một khung có thể bị **chia** qua nhiều lần `recv`, kể cả header bị cắt (ví dụ mới nhận 5/12 byte header).
- **Giới hạn**: `length` bằng 0 hoặc lớn hơn 65536 là lỗi giao thức: gửi `ERR_PROTOCOL` (nếu còn gửi được) rồi **đóng kết nối**, vì không thể đồng bộ lại ranh giới khung.
- Khung nhỏ nhất là 13 byte (12 header + 1 payload); khung điển hình `MSG_SEND` dài 12 + 70 = 82 byte, trong đó header chiếm khoảng 15 %.

Ví dụ khung `PING` (payload 26 byte, `seq = 5`, `ack = 4`), chưa mã hóa và đã mã hóa với khóa 3:

```
header : 00 00 00 1A  00 00 00 05  00 00 00 04
rõ     : 7B 22 74 79 70 65 22 3A 22 50 49 4E 47 22 2C 22 69 64 22 3A 22 6B 2D 31 22 7D
mã hóa : 7E 25 77 7C 73 68 25 3D 25 53 4C 51 4A 25 2F 25 6C 67 25 3D 25 6E 30 34 25 80
```

---

## 3. Cấu trúc message (JSON)

Có 3 loại message, phân biệt bằng trường `type`.

### 3.1 Request (client → server)

```json
{ "type": "LOGIN", "id": "c-17", "data": { "username": "an", "password": "123456" } }
```

| Trường | Bắt buộc | Mô tả |
|---|---|---|
| `type` | có | Tên lệnh, IN HOA (mục 5). |
| `id` | có | Chuỗi duy nhất trong một kết nối. Giao diện dùng tiền tố `c-` (`c-1`, `c-2`...), `netchat-core` dùng `k-` cho message do nó tự gửi (`HELLO`, `PING`, `RESUME`). Server trả lại nguyên trong response. |
| `data` | tùy lệnh | Đối tượng tham số. |

### 3.2 Response (server → client, trả lời một request)

```json
{ "type": "LOGIN_RES", "id": "c-17", "ok": true, "data": { "token": "..." } }
{ "type": "LOGIN_RES", "id": "c-17", "ok": false, "error": { "code": 1002, "message": "Sai mật khẩu" } }
```

- `type` = tên request + `_RES`; `id` = `id` của request; `ok` = `true`/`false`.
- `ok = false` thì có `error`; ngược lại có `data` (có thể vắng nếu không có gì trả về).

### 3.3 Event (server → client, tự đẩy)

```json
{ "type": "EVT_MSG", "data": { "msg_id": 42, "from": "an", "text": "hi", "ts": 1761412345678 } }
```

`type` luôn bắt đầu bằng `EVT_`; không có `id` và `ok`.

### 3.4 Quy ước chung

- Tên trường JSON dùng `snake_case`; `type` dùng `UPPER_SNAKE_CASE`.
- Thời gian `ts` là **Unix epoch tính bằng mili giây** (số nguyên).
- `msg_id`, `group_id` là số nguyên do server cấp, tăng dần (an toàn trong JavaScript vì < 2⁵³).
- Người dùng được định danh bằng `username` (duy nhất, server lưu chữ thường).
- Server **bỏ qua** trường lạ trong `data`; thiếu trường bắt buộc hoặc sai kiểu thì trả `ERR_BAD_REQUEST` (code 1).
- `text` tối đa 4000 byte UTF-8.

---

## 4. Phiên làm việc

### 4.1 Trạng thái kết nối

```
CONNECTED ──LOGIN/RESUME ok──> AUTHENTICATED ──LOGOUT / đóng / timeout──> CLOSED
    │
    └── 30 s không đăng nhập ──> CLOSED
```

- Ngay sau khi kết nối, kết nối ở trạng thái `CONNECTED`. Chỉ được gửi `HELLO`, `REGISTER`, `LOGIN`, `RESUME`, `PING` (và các message `dev` ở mục 5.8 khi bật). Lệnh khác trả `ERR_NOT_AUTHENTICATED` (code 2).
- Kết nối chưa xác thực quá **30 giây** bị server đóng.

### 4.2 HELLO: thương lượng mã hóa và nhịp heartbeat

```json
C→S {"type":"HELLO","id":"k-1","data":{"proto":1,"client":"core/0.1","enc":["caesar"],"ping_ms":1000}}
S→C {"type":"HELLO_RES","id":"k-1","ok":true,"data":{"proto":1,"enc":"caesar","ping_ms":1000,"conn_id":17,"max_payload":65536}}
```

| Trường | Ý nghĩa |
|---|---|
| `enc` (request) | Danh sách thuật toán mã hóa client hỗ trợ: `"caesar"`. Rỗng hoặc vắng là không mã hóa. |
| `enc` (response) | Thuật toán server chọn: `"caesar"` hoặc `"none"`. |
| `ping_ms` | Chu kỳ `PING` đề nghị (500–60000, mặc định 20000). Server chọn giá trị cuối. Audit bật thì client đề nghị 1000. |
| `conn_id` | Số định danh kết nối ở server; dùng để ghép log hai phía (mục 9.6). |

- `HELLO` là tùy chọn. Kết nối không gửi `HELLO` chạy ở chế độ `enc = none`, `ping_ms = 20000`.
- `HELLO` và `HELLO_RES` luôn ở dạng rõ. Client **gửi `HELLO` một mình và chờ `HELLO_RES`**; từ khung kế tiếp, mỗi chiều mã hóa payload theo `enc` đã chọn. (Nếu client gửi thêm khung trước khi nhận `HELLO_RES`, server không biết khung đó rõ hay đã mã hóa.)

### 4.3 Token và khôi phục phiên

- `LOGIN` thành công trả về `token` (chuỗi ngẫu nhiên 32 byte hex), gắn với `username`.
- Khi mất kết nối đột ngột, phiên được **giữ 60 giây**. Trong thời gian đó client mở kết nối mới, gửi `HELLO` rồi `RESUME` với `token` để vào lại mà không cần mật khẩu.
- `LOGOUT` hoặc hết 60 giây thì token bị hủy.
- Trạng thái online/offline của user tính theo **kết nối đang mở**, không tính theo thời gian giữ phiên.
- Mỗi kết nối TCP mới có không gian `seq`/`ack` riêng, bắt đầu lại từ 1.

### 4.4 Đăng nhập trùng

Mỗi tài khoản chỉ có **một kết nối** hoạt động. Khi đăng nhập từ nơi thứ hai, kết nối cũ nhận `EVT_KICKED` với `reason = "login_elsewhere"` rồi bị đóng; kết nối mới đăng nhập bình thường.

### 4.5 Heartbeat và timeout

- Mỗi bên có thể gửi `PING`; bên kia trả `PONG` ngay (giữ nguyên `id`).
- Chu kỳ `PING` là `ping_ms` đã thương lượng ở `HELLO`. Khi **audit bật**, cả hai bên gửi `PING` theo chu kỳ này (để phát hiện khung mất ở cuối luồng, mục 9.3). Khi audit tắt, client gửi `PING` mỗi 20 giây lúc rảnh.
- Server đóng kết nối nếu **60 giây** không nhận được byte nào từ client.
- Khi server đóng một kết nối đã xác thực, server đẩy `EVT_USER_OFFLINE` cho các bạn bè đang online và `EVT_PEER_DISCONNECTED` cho những người đang mở chat 1-1 với user đó.

### 4.6 Kết nối lại phía client

`netchat-core` tự kết nối lại khi mất kết nối:

1. Báo `STATUS reconnecting` lên giao diện.
2. Thử lại theo **backoff mũ có jitter**: 0.5 s, 1 s, 2 s... tối đa 5 s, cộng ngẫu nhiên ±20 %.
3. Kết nối được thì gửi `HELLO`, rồi `RESUME` với `token` đã được main đưa cho lõi (`set_session`). Lõi dựng `RESUME` bằng `snprintf` (token là hex nên không cần escape).
4. Thành công báo `restored`, sau đó `open`. Quá 60 giây hoặc nhận `ERR_TOKEN_INVALID` thì báo `closed` để giao diện về màn đăng nhập.

Token chỉ nằm trong bộ nhớ của main và lõi. Tải lại renderer không làm đứt kết nối TCP.

---

## 5. Danh sách message

Ký hiệu: **C→S** request của client, **S→C** response hoặc event. Mọi request đều có response `<TYPE>_RES` với `ok`/`error`; các bảng chỉ liệt kê `data` của request và `data` khi thành công.

### 5.1 Tài khoản

| Type | Cần đăng nhập | `data` request | `data` response (ok) |
|---|---|---|---|
| `REGISTER` | không | `username` (3–20 ký tự `a-z 0-9 _`), `password` (6–64 ký tự), `display_name` (1–30 ký tự) | `{}` |
| `LOGIN` | không | `username`, `password` | `token`, `username`, `display_name` |
| `RESUME` | không | `token` | `username`, `display_name` |
| `LOGOUT` | có | — | `{}` (sau đó server đóng kết nối) |
| `CHANGE_PASSWORD` | có | `old_password`, `new_password` | `{}` |
| `UPDATE_PROFILE` | có | `display_name` | `display_name` |

Mật khẩu nằm trong payload nên đi trên dây ở dạng đã mã hóa (mục 8). Server **băm SHA-256 + salt ngẫu nhiên theo từng user** trước khi lưu, không bao giờ lưu mật khẩu thô.

### 5.2 Hiện diện (presence)

| Type | `data` request | `data` response (ok) |
|---|---|---|
| `ONLINE_LIST` | — | `users`: mảng `{ "username", "display_name" }` của mọi user đang online |

| Event | Khi nào | `data` |
|---|---|---|
| `EVT_USER_ONLINE` | một **bạn bè** đăng nhập | `username` |
| `EVT_USER_OFFLINE` | một **bạn bè** ngắt kết nối | `username` |

### 5.3 Bạn bè

| Type | `data` request | `data` response (ok) |
|---|---|---|
| `FRIEND_REQUEST` | `to` (username) | `{}` |
| `FRIEND_ACCEPT` | `from` (username đã gửi lời mời) | `{}` |
| `FRIEND_REJECT` | `from` | `{}` |
| `FRIEND_REMOVE` | `username` | `{}` |
| `FRIEND_LIST` | — | `friends`: mảng `{ "username", "display_name", "online" }`; `pending_in`: mảng username đã mời mình; `pending_out`: mảng username mình đã mời |

| Event | Gửi cho | `data` |
|---|---|---|
| `EVT_FRIEND_REQUEST` | người nhận lời mời | `from`, `display_name` |
| `EVT_FRIEND_ACCEPTED` | người đã mời | `username`, `display_name` |
| `EVT_FRIEND_REJECTED` | người đã mời | `username` |
| `EVT_FRIEND_REMOVED` | người bị hủy kết bạn | `username` |

Quy tắc: không tự kết bạn với chính mình (`ERR_BAD_REQUEST`); đã là bạn thì `ERR_ALREADY_EXISTS` (6); lời mời gửi lúc người kia offline vẫn được lưu và hiện trong `pending_in` khi họ đăng nhập.

### 5.4 Chat 1-1

Chỉ được nhắn cho **bạn bè**; người lạ trả `ERR_NOT_FRIENDS` (4).

| Type | `data` request | `data` response (ok) |
|---|---|---|
| `MSG_SEND` | `to` (username), `text` | `msg_id`, `ts`, `delivered` (`true` nếu người nhận đang online và đã được đẩy tin; `false` nếu đã lưu offline) |
| `CHAT_OPEN` | `with` (username) | `history`: tối đa 50 tin gần nhất `{ "msg_id","from","text","ts" }`, tăng dần theo thời gian |
| `CHAT_CLOSE` | `with` | `{}` |

`CHAT_OPEN` dùng khi người dùng chuyển sang một cuộc trò chuyện khác; `CHAT_CLOSE` là yêu cầu **dừng cuộc trò chuyện**.

| Event | Gửi cho | `data` |
|---|---|---|
| `EVT_MSG` | người nhận | `msg_id`, `from`, `text`, `ts` |
| `EVT_CHAT_CLOSED` | bên còn lại, khi một bên gửi `CHAT_CLOSE` | `username` |
| `EVT_PEER_DISCONNECTED` | bên còn lại, khi một bên ngắt kết nối hoặc rớt mạng | `username` |

### 5.5 Nhóm chat

Chỉ **thành viên** mới gửi tin/xem được nhóm; không phải thành viên trả `ERR_NOT_MEMBER` (5). Người tạo nhóm là `owner`.

| Type | Ai được gọi | `data` request | `data` response (ok) |
|---|---|---|---|
| `GROUP_CREATE` | mọi user | `name` (1–40 ký tự) | `group_id` |
| `GROUP_INVITE` | thành viên | `group_id`, `username` (phải là bạn của người mời) | `{}` |
| `GROUP_ACCEPT` | người được mời | `group_id` | `members` |
| `GROUP_DECLINE` | người được mời | `group_id` | `{}` |
| `GROUP_KICK` | **owner** | `group_id`, `username` | `{}` |
| `GROUP_LEAVE` | thành viên | `group_id` | `{}` |
| `GROUP_LIST` | mọi user | — | `groups`: mảng `{ "group_id","name","owner" }`; `invites`: mảng `{ "group_id","name","from" }` |
| `GROUP_MEMBERS` | thành viên | `group_id` | `members`: mảng `{ "username","display_name","online" }` |
| `GROUP_MSG_SEND` | thành viên | `group_id`, `text` | `msg_id`, `ts` |
| `GROUP_HISTORY` | thành viên | `group_id` | `history`: tối đa 50 tin gần nhất `{ "msg_id","from","text","ts" }` |

Nếu `owner` rời nhóm thì quyền owner chuyển cho thành viên vào nhóm sớm nhất; nhóm hết người thì bị xóa.

| Event | Gửi cho | `data` |
|---|---|---|
| `EVT_GROUP_INVITE` | người được mời | `group_id`, `name`, `from` |
| `EVT_GROUP_JOINED` | các thành viên hiện có | `group_id`, `username` |
| `EVT_GROUP_LEFT` | các thành viên còn lại | `group_id`, `username` |
| `EVT_GROUP_KICKED` | người bị đuổi (các thành viên còn lại nhận `EVT_GROUP_LEFT`) | `group_id` |
| `EVT_GROUP_MSG` | mọi thành viên **trừ người gửi** | `group_id`, `msg_id`, `from`, `text`, `ts` |

### 5.6 Tin nhắn offline

- Khi `MSG_SEND`/`GROUP_MSG_SEND` mà người nhận **không có kết nối**, server lưu tin vào DB với trạng thái chưa giao.
- Ngay sau khi `LOGIN`/`RESUME` thành công, server đẩy các tin chưa giao bằng một event:

```json
{ "type": "EVT_OFFLINE_MSGS", "data": { "messages": [
  { "kind": "direct", "msg_id": 40, "from": "binh", "text": "alo", "ts": 1761410000000 },
  { "kind": "group",  "msg_id": 41, "group_id": 3, "from": "chi", "text": "họp nhé", "ts": 1761410100000 }
] } }
```

- Mỗi batch tối đa 100 tin, sắp theo `msg_id` tăng dần.
- Client xử lý xong thì gửi `OFFLINE_ACK`:

| Type | `data` request | `data` response (ok) |
|---|---|---|
| `OFFLINE_ACK` | `up_to_msg_id` (số lớn nhất client đã nhận trong batch) | `{}` |

- Server đánh dấu đã giao các tin **của riêng người này** có `msg_id` ≤ mốc trong batch vừa gửi, rồi đẩy batch kế nếu còn.
- Client rớt trước khi gửi ACK thì tin vẫn chưa giao và được gửi lại ở lần đăng nhập sau (giao ít nhất một lần; client khử trùng theo `msg_id`).

### 5.7 Hệ thống

| Type | Hướng | Ghi chú |
|---|---|---|
| `HELLO` | C→S | Mục 4.2 |
| `PING` | C→S hoặc S→C | `data` không bắt buộc |
| `PONG` | phản hồi của `PING` | giữ nguyên `id` |
| `EVT_KICKED` | S→C | `data.reason`: `login_elsewhere` \| `timeout` \| `server_shutdown` \| `protocol_error` |

### 5.8 Message dev (chỉ khi server chạy với `NETCHAT_DEV=1`)

Không bật `NETCHAT_DEV` thì các type này trả `ERR_BAD_REQUEST` như type lạ.

| Type | Cần đăng nhập | `data` request | `data` response (ok) |
|---|---|---|---|
| `ECHO` | không | tùy ý | nguyên `data` đã gửi. Dùng để đo riêng đường mạng. |
| `STATS` | không | — | bộ đếm của server (mục 11.3) |
| `FAULT` | không | `drop_percent` (0–100) | `{}`. Từ lúc này server bỏ ngẫu nhiên tỷ lệ đó các khung gửi **cho kết nối này** (mục 9.5). |

---

## 6. Mã lỗi

| Code | Tên | Ý nghĩa |
|---|---|---|
| 1 | `ERR_BAD_REQUEST` | JSON hỏng, thiếu trường, sai kiểu, giá trị ngoài phạm vi, `type` không biết |
| 2 | `ERR_NOT_AUTHENTICATED` | Chưa đăng nhập mà gửi lệnh cần đăng nhập |
| 3 | `ERR_PERMISSION` | Không đủ quyền (ví dụ không phải owner mà `GROUP_KICK`) |
| 4 | `ERR_NOT_FRIENDS` | Nhắn hoặc mời nhóm cho người không phải bạn bè |
| 5 | `ERR_NOT_MEMBER` | Không phải thành viên nhóm |
| 6 | `ERR_ALREADY_EXISTS` | Trùng: username đã dùng, đã là bạn, đã là thành viên, đã gửi lời mời |
| 7 | `ERR_NOT_FOUND` | Không có user/nhóm/lời mời tương ứng |
| 8 | `ERR_TOO_LARGE` | Vượt giới hạn kích thước (mục 7) |
| 9 | `ERR_RATE_LIMIT` | Gửi quá nhanh |
| 10 | `ERR_PROTOCOL` | Lỗi khung; sau lỗi này kết nối bị đóng |
| 11 | `ERR_INTERNAL` | Lỗi phía server (DB, hết bộ nhớ...) |
| 1001 | `ERR_USER_NOT_FOUND` | `LOGIN` với username không tồn tại |
| 1002 | `ERR_WRONG_PASSWORD` | Sai mật khẩu |
| 1003 | `ERR_TOKEN_INVALID` | `RESUME` với token sai hoặc đã hết hạn |

Mã **1–99** là lỗi chung, mã **1000+** là lỗi riêng của nhóm tài khoản. `message` là chuỗi cho người đọc; client dựa vào `code`.

---

## 7. Giới hạn

| Mục | Giá trị |
|---|---|
| Kích thước payload tối đa | 65536 byte |
| Header khung | 12 byte |
| `text` tối đa | 4000 byte |
| Thời gian chờ đăng nhập | 30 giây |
| Thời gian không hoạt động tối đa | 60 giây |
| Thời gian giữ phiên sau khi rớt | 60 giây |
| Tốc độ gửi tối đa mỗi kết nối | 20 message/giây (vượt thì `ERR_RATE_LIMIT`); `NETCHAT_RATE_LIMIT=<n>` đổi giá trị, `0` là không giới hạn |
| Hàng đợi gửi tối đa mỗi kết nối | 1 MB; vượt thì server đóng kết nối (client đọc quá chậm) |
| Request đang chờ trả lời mỗi kết nối | 64 (giao diện xếp hàng phần vượt) |
| Số thành viên tối đa một nhóm | 100 |
| Số tin trong `history` | 50 |

---

## 8. Mã hóa đường truyền (Caesar)

Khi `HELLO_RES.enc = "caesar"`, **toàn bộ payload** (không phải header) được dịch từng byte:

```c
void cipher_encode(uint8_t *p, size_t n) { for (size_t i = 0; i < n; i++) p[i] = (uint8_t)(p[i] + NETCHAT_CAESAR_KEY); }
void cipher_decode(uint8_t *p, size_t n) { for (size_t i = 0; i < n; i++) p[i] = (uint8_t)(p[i] - NETCHAT_CAESAR_KEY); }
```

- `NETCHAT_CAESAR_KEY` là hằng số cố định (3), định nghĩa một lần trong `common/cipher.h`; client và server cùng include. Không cấu hình, không trao đổi khóa.
- Dịch theo **byte** nên chạy đúng với tiếng Việt UTF-8 và mọi ký tự JSON; độ dài payload không đổi, nên `length` và giới hạn 65536 không bị ảnh hưởng.
- Mã hóa và giải mã **tại chỗ**, không cấp phát thêm bộ nhớ.
- Vị trí gọi:
  - **Nhận**: ngay sau khi `frame_decoder_next` trả `FRAME_OK`, giải mã trong buffer của decoder (dấu `\0` kết thúc nằm sau payload nên không bị đổi; cJSON đọc trực tiếp được).
  - **Gửi**: ngay sau khi `frame_encode` đã chép payload vào hàng đợi gửi, mã hóa phần sau header.
- Tầng nghiệp vụ (`msg`, handler, giao diện) không biết mã hóa tồn tại.
- Mật khẩu đi trong payload nên được che theo cùng cách; việc lưu trữ vẫn dùng SHA-256 + salt ở server.
- Caesar là phép che dữ liệu đơn giản, **không phải bảo mật**: người nghe được đường truyền có thể thử 255 khóa trong thời gian rất ngắn. Phép mã hóa nằm trong hai hàm trên nên thay được bằng thuật toán khác (tên khác trong `HELLO.enc`) mà không đổi gì ở nơi khác.
- Test: mã hóa rồi giải mã trả đúng bản gốc với mọi giá trị byte, chuỗi rỗng, tiếng Việt, payload 65536 byte.

---

## 9. Audit log mạng

Audit log ghi **mọi thứ quan sát được ở tầng mạng**, cả ở server lẫn client, theo cùng một định dạng, để thấy ngay lúc một khung hay một đoạn TCP bị mất. Đây là nhật ký hoạt động của hệ thống ở tầng mạng: kết nối, khung, thứ tự và đường truyền.

### 9.1 Mất gói xảy ra ở đâu và được ghi bằng gì

| Tầng | "Mất" nghĩa là gì | Ai thấy | Sự kiện audit |
|---|---|---|---|
| Segment TCP trên đường truyền | Kernel phát hiện mất và **truyền lại**; dữ liệu vẫn đến đủ, đúng thứ tự, chỉ chậm hơn | Kernel của người gửi | `TCP_RETRANS` |
| Khung trong một kết nối | Một khung được gửi mà không tới (do tầng ứng dụng bỏ, lỗi, hoặc `FAULT`): người nhận thấy `seq` nhảy cóc | Người nhận, ngay khi khung kế tiếp tới | `GAP` |
| Khung ở **cuối luồng** (không có khung nào sau nó) | Không có khung kế để lộ chỗ nhảy cóc; `ack` trong `PONG` thấp hơn `seq` của `PING` đã gửi | Người gửi, khi `PONG` về | `UNACKED`, `NO_PONG` |
| Khung đang bay khi kết nối đứt | Dữ liệu còn trong buffer chưa tới nơi hoặc chưa được ack bị mất cùng kết nối | Cả hai bên sau khi đóng | `CONN_CLOSE` (kèm khoảng `seq` nghi mất), `REQ_ABORTED`, `LOST_ON_CLOSE` |
| Hàng đợi gửi vượt 1 MB | Server đóng kết nối client đọc chậm; khung chưa gửi bị bỏ | Server | `QUEUE_HIGH`, `QUEUE_FULL`, `LOST_ON_CLOSE` |
| Request không có response | Request đã gửi nhưng không nhận trả lời trong thời hạn | Client | `REQ_TIMEOUT` |

Trong điều kiện TCP bình thường, trong **một kết nối** dữ liệu không bao giờ nhảy cóc: mất gói mạng chỉ hiện ra qua `TCP_RETRANS` và độ trễ tăng. `GAP` và `UNACKED` xuất hiện khi có khung bị bỏ trước khi tới socket (lỗi, hàng đợi, hoặc cố ý bằng `FAULT`, mục 9.5).

### 9.2 Định dạng một dòng

```
<thời gian> AUDIT <bên> <định danh> ev=<SỰ_KIỆN> [khóa=giá_trị ...] [!!]
```

| Phần | Ý nghĩa |
|---|---|
| thời gian | `YYYY-MM-DD HH:MM:SS.uuuuuu` (đồng hồ hệ thống, micro giây) |
| bên | `S` = server, `C` = netchat-core |
| định danh | server: `conn=17 user=an` (user vắng nếu chưa đăng nhập); client: `who=an` (tên profile hoặc username) |
| `!!` | dấu cuối dòng cho sự kiện **bất thường** (`GAP`, `UNACKED`, `TCP_RETRANS`, `FAULT_DROP`, `REQ_TIMEOUT`...) để lọc bằng `grep '!!$'` |

Mỗi dòng ghi ra **file** và, khi `NETCHAT_AUDIT_STDERR=1`, ra **stderr** để xem trực tiếp trong terminal.

### 9.3 Cơ chế phát hiện

**`seq` liên tục.** Mỗi bên giữ `rx_last` (seq khung cuối nhận được). Khi nhận khung có `seq = s`:

| Điều kiện | Việc làm |
|---|---|
| `s == rx_last + 1` | Bình thường. |
| `s > rx_last + 1` | Ghi `GAP expected=<rx_last+1> got=<s> missing=<s-rx_last-1>`; vẫn xử lý khung; đặt `rx_last = s`. |
| `s ≤ rx_last` | Ghi `DUP seq=<s>`; bỏ khung. |

**`ack` và `PING`/`PONG`.** Khi bên A gửi `PING` với `seq = P`, bên B nhận được `PING` thì cũng đã nhận mọi khung trước đó; `PONG` của B mang `ack ≥ P`. Khi `PONG` về, A kiểm tra: nếu `ack < P` thì ghi `UNACKED from=<ack+1> to=<P>` (các khung đó chưa tới B). Nếu quá `3 × ping_ms` mà không có `PONG` thì ghi `NO_PONG ping_seq=<P>`. Cơ chế này bắt được khung mất ở **cuối luồng**.

**`TCP_INFO`.** Cứ mỗi `NETCHAT_AUDIT_TCPINFO_MS` (mặc định 200) mỗi kết nối được đọc bộ đếm TCP của kernel (`getsockopt(TCP_INFO)` trên Linux; `WSAIoctl(SIO_TCP_INFO)` trên Windows). Nếu số segment truyền lại tăng thì ghi `TCP_RETRANS`.

**Tổng kết khi đóng.** Mỗi bên giữ `tx_seq` (seq khung cuối đã gửi), `peer_ack` (ack lớn nhất nhận được từ bên kia), số khung còn trong hàng đợi gửi. Khi đóng, khung có `seq` trong `(peer_ack, tx_seq]` là **nghi mất**, và khung còn trong hàng đợi chưa tới socket là **chắc chắn không tới**.

**Request.** Phía client lõi theo dõi `id` của mọi request đang chờ (đọc `id` từ JSON); response về thì xóa; quá 5 giây không trả lời thì `REQ_TIMEOUT`; kết nối đóng khi còn request chờ thì `REQ_ABORTED` cho từng cái.

### 9.4 Danh mục sự kiện

| Sự kiện | Mức | Trường chính | Bên |
|---|---|---|---|
| `CONN_OPEN` | events | `peer`/`local`, `conn` | S, C |
| `HELLO` | events | `enc`, `ping_ms`, `server_conn` | S, C |
| `CONN_CLOSE` | events | `reason`, `tx_seq`, `rx_seq`, `peer_ack`, `suspect=<từ>-<đến>`, `unsent_frames`, `unsent_bytes`, `pending_req` | S, C |
| `RECONNECT` | events | `attempt`, `delay_ms`, `result` | C |
| `GAP` `!!` | events | `expected`, `got`, `missing` | S, C |
| `DUP` `!!` | events | `seq` | S, C |
| `UNACKED` `!!` | events | `from`, `to`, `ping_seq` | S, C |
| `NO_PONG` `!!` | events | `ping_seq`, `waited_ms` | S, C |
| `TCP_RETRANS` `!!` | events | `retrans_total`, `delta`, `rtt_us`, `cwnd`, `unacked` | S, C |
| `FAULT_DROP` `!!` | events | `seq`, `len`, `type` | S, C |
| `QUEUE_HIGH` `!!` | events | `bytes`, `limit` (khi đạt 50 %) | S |
| `QUEUE_FULL` `!!` | events | `bytes`, `dropped_frames` | S |
| `LOST_ON_CLOSE` `!!` | events | `first_seq`, `last_seq`, `frames`, `bytes` | S, C |
| `FRAME_ERR` `!!` | events | `length`, `reason` | S, C |
| `REQ_TIMEOUT` `!!` | events | `id`, `type`, `waited_ms` | C |
| `REQ_ABORTED` `!!` | events | `id`, `type` | C |
| `STAT` | events | `conns`, `frames_in`, `frames_out`, `bytes_in`, `bytes_out`, `retrans` (mỗi giây) | S |
| `FRAME_TX` | frames | `seq`, `ack`, `len`, `type`, `id` | S, C |
| `FRAME_RX` | frames | `seq`, `ack`, `len`, `type`, `id` | S, C |
| `RTT` | frames | `rtt_us` (từ `PING`/`PONG`) | S, C |

### 9.5 Cấu hình và mô phỏng mất gói

| Biến môi trường | Giá trị | Ý nghĩa |
|---|---|---|
| `NETCHAT_AUDIT` | `off` \| `events` \| `frames` | Mức ghi. `events` = mọi sự kiện trừ `FRAME_*` và `RTT`; `frames` = tất cả (mỗi khung một dòng, dành cho vài kết nối). Mặc định `events`. |
| `NETCHAT_AUDIT_FILE` | đường dẫn | File log. Mặc định `logs/audit-server.log`, `logs/audit-<profile>.log` ở client. |
| `NETCHAT_AUDIT_STDERR` | `1` | In thêm ra stderr. |
| `NETCHAT_AUDIT_TCPINFO_MS` | số | Chu kỳ đọc `TCP_INFO` (mặc định 200). |
| `NETCHAT_FAULT_DROP` | 0–100 | (cần `NETCHAT_DEV=1`) Phần trăm khung gửi ra bị bỏ ngẫu nhiên. |
| `NETCHAT_FAULT_SEED` | số | Hạt giống để lần chạy lặp lại được. |

- `FAULT_DROP` hoạt động như sau: khung đã được gán `seq`, nhưng **không** được đưa vào hàng đợi gửi; bên gửi ghi `FAULT_DROP`, bên nhận thấy `GAP` ở khung kế tiếp (hoặc `UNACKED` nếu đó là khung cuối). Có thể bật lúc chạy bằng message `FAULT` (server) hoặc lệnh `{"cmd":"fault","drop_percent":10}` (lõi), không cần khởi động lại.
- Mất gói **thật của TCP** được mô phỏng trong WSL bằng `sudo tc qdisc add dev lo root netem delay 20ms loss 1%`; audit chỉ ghi `TCP_RETRANS` và độ trễ tăng, không có `GAP`. Xóa bằng `sudo tc qdisc del dev lo root`.
- Audit nhiều kết nối tốn CPU (mỗi kết nối một lần đọc `TCP_INFO` mỗi 200 ms và `PING` mỗi giây). Khi đo hiệu năng quy mô lớn, đặt `NETCHAT_AUDIT=off`.

### 9.6 Ghép log hai phía

`HELLO_RES.conn_id` cho client biết số kết nối ở server; client ghi `server_conn=<n>` ngay sau đó. Công cụ `tools/audit/merge.py server.log client.log`:

1. Trộn hai log theo thời gian (hai bên chạy cùng máy nên dùng chung đồng hồ).
2. Ghép từng cặp theo `(server_conn, seq, chiều)`: mọi `FRAME_TX` phải có `FRAME_RX` tương ứng.
3. In ra các khung có `TX` mà không có `RX` thành dòng `LOST dir=S→C seq=42 type=EVT_MSG`.

### 9.7 Ví dụ: server bỏ một `EVT_MSG`

Bật `FAULT` cho kết nối của `binh`:

```
server  2026-11-07 17:42:10.120345 AUDIT S conn=17 user=binh ev=FRAME_TX seq=41 ack=30 len=89 type=EVT_MSG
server  2026-11-07 17:42:10.120398 AUDIT S conn=17 user=binh ev=FAULT_DROP seq=42 len=89 type=EVT_MSG !!
server  2026-11-07 17:42:10.120401 AUDIT S conn=17 user=binh ev=FRAME_TX seq=43 ack=30 len=89 type=EVT_MSG
client  2026-11-07 17:42:10.121002 AUDIT C who=binh ev=FRAME_RX seq=41 ack=29 len=89 type=EVT_MSG
client  2026-11-07 17:42:10.121050 AUDIT C who=binh ev=GAP expected=42 got=43 missing=1 !!
client  2026-11-07 17:42:10.121052 AUDIT C who=binh ev=FRAME_RX seq=43 ack=29 len=89 type=EVT_MSG
```

Hai bên cùng in dòng bất thường trong vòng vài trăm micro giây. Trường hợp khung bị bỏ là khung cuối cùng:

```
client  2026-11-07 17:42:30.500000 AUDIT C who=binh ev=FRAME_TX seq=88 ack=70 len=26 type=PING
client  2026-11-07 17:42:30.500310 AUDIT C who=binh ev=UNACKED from=87 to=88 ping_seq=88 !!
```

Và khi mạng đứt giữa chừng:

```
server  2026-11-07 17:43:02.004100 AUDIT S conn=17 user=binh ev=CONN_CLOSE reason=recv_error tx_seq=130 rx_seq=95 peer_ack=127 suspect=128-130 unsent_frames=2 unsent_bytes=178 !!
server  2026-11-07 17:43:02.004104 AUDIT S conn=17 user=binh ev=LOST_ON_CLOSE first_seq=129 last_seq=130 frames=2 bytes=178 !!
client  2026-11-07 17:43:02.004500 AUDIT C who=binh ev=CONN_CLOSE reason=reset tx_seq=95 rx_seq=127 peer_ack=95 pending_req=1
client  2026-11-07 17:43:02.004502 AUDIT C who=binh ev=REQ_ABORTED id=c-31 type=MSG_SEND !!
```

### 9.8 Hiển thị trong giao diện

Lõi gửi mỗi dòng audit lên main bằng kind `AUDIT` (mục 10.3). Panel dev của giao diện có thẻ "Mạng", tô đậm dòng có `!!`, lọc theo sự kiện, và có nút "Mô phỏng mất 10 %" gọi lệnh `fault`.

---

## 10. Lõi mạng C `netchat-core` và Electron

### 10.1 Chức năng của lõi

Lõi là một chương trình C độc lập, được Electron main khởi chạy. Nó thực hiện:

- `connect` tới server (`TCP_NODELAY`, `SO_KEEPALIVE`), `HELLO`, bật mã hóa.
- Đóng khung (gộp header + payload vào một buffer, một `send()`), lặp `send()` khi gửi thiếu; ghép khung khi nhận (dùng chính `frame_decoder` của server).
- Gán `seq`, điền `ack`, kiểm tra `seq` của khung nhận (mục 9.3).
- `PING` theo `ping_ms`, kết nối lại + `RESUME` (mục 4.6), theo dõi request chờ, đọc `TCP_INFO`, ghi audit, đếm thống kê.
- Dùng cJSON (có sẵn trong repo) để đọc `type`/`id` và `HELLO_RES`; mọi message khác được chuyển nguyên văn.

Hai thread blocking: **RX** (`recv` → ghép khung → giải mã → ghi ra stdout) và **TX** (đọc stdin → mã hóa → đóng khung → `send`; cũng chạy bộ đếm giờ `PING`, `TCP_INFO`, timeout request). Một mutex bảo vệ trạng thái socket và một mutex riêng cho stdout.

### 10.2 Cấu trúc thư mục

```
net-chat/
├─ chuc-nang.md · protocol-v2.md · schema.sql · docker-compose.yml
├─ common/                     mã C dùng chung cho server, lõi client, bộ tạo tải
│  ├─ frame.c / frame.h        khung 12 byte: decoder + encode
│  ├─ cipher.c / cipher.h      Caesar và NETCHAT_CAESAR_KEY
│  ├─ audit.c / audit.h        định dạng và ghi dòng audit
│  └─ compat.h                 Winsock ↔ POSIX
├─ server/                     Server C (net.c, msg.c, auth.c, store.c, server.c, cJSON/)
├─ client/
│  ├─ native/                  lõi mạng C: src/core.c · conn.c · pipe.c, Makefile, build.ps1, bin/
│  ├─ electron/                main.cjs · preload.cjs · core-link.cjs
│  └─ src/                     React
└─ tools/
   ├─ bots/                    bot Python
   ├─ loadgen/                 bộ tạo tải C (epoll)
   ├─ audit/                   merge.py
   └─ bench/                   tổng hợp số liệu
```

### 10.3 Giao thức pipe giữa Electron main và lõi

Đây là kết nối cục bộ (cùng máy, qua stdin/stdout của lõi). Ống dẫn cũng là luồng byte nên dùng cùng cách đóng khung:

```
[uint32 BE length][uint8 kind][body]        length = 1 + len(body)
```

| Hướng | kind | Thân | Ý nghĩa |
|---|---|---|---|
| main → lõi | `0x01 SEND` | JSON gửi lên server | Lõi mã hóa, đóng khung, `send()` |
| main → lõi | `0x02 CTRL` | `{"cmd":"connect","host","port","audit","audit_file"}`, `{"cmd":"close"}`, `{"cmd":"set_session","token"}`, `{"cmd":"stats"}`, `{"cmd":"fault","drop_percent"}`, `{"cmd":"audit","level"}` | Điều khiển |
| lõi → main | `0x81 RECV` | JSON server gửi về (đã giải mã) | Response hoặc `EVT_*` |
| lõi → main | `0x82 STATUS` | `{"state":"connecting\|open\|reconnecting\|restored\|closed","reason":"..."}` | Trạng thái kết nối |
| lõi → main | `0x83 STATS` | bộ đếm phía client | Panel dev |
| lõi → main | `0x84 AUDIT` | một dòng audit (văn bản) | Thẻ "Mạng" |

`stderr` của lõi dành cho log chẩn đoán, không lẫn với dữ liệu. Lõi tự thoát khi stdin đóng (không để tiến trình mồ côi).

### 10.4 Giao diện `window.netchat` (renderer)

```ts
interface NetChatApi {
  connect(opts?: { host?: string; port?: number }): Promise<void>
  disconnect(): Promise<void>
  request<T = unknown>(type: string, data?: object):
      Promise<{ ok: true; data: T } | { ok: false; error: { code: number; message: string } }>
  onEvent(cb: (evt: { type: `EVT_${string}`; data: any }) => void): () => void
  onStatus(cb: (s: ConnectionStatus) => void): () => void
  onAudit(cb: (line: string) => void): () => void
  setFault(dropPercent: number): Promise<void>
  getStats(): Promise<ConnStats>
}
```

Main giữ bảng `id → Promise` (tiền tố `c-`), đưa `token` cho lõi khi thấy `LOGIN_RES`/`RESUME_RES` thành công. Renderer chỉ thấy `error.code`.

### 10.5 Thiết lập Electron

- `main.cjs`: `preload: path.join(__dirname, 'preload.cjs')`, `contextIsolation: true`, `nodeIntegration: false`, `sandbox: true`. Mở lõi bằng `child_process.spawn(corePath, [], { stdio: ['pipe','pipe','pipe'] })`, đóng lõi ở `before-quit`.
- Đường dẫn lõi: `process.env.NETCHAT_CORE ?? path.join(__dirname, '../native/bin/netchat-core.exe')`.
- `preload.cjs` chỉ phơi các hàm ở mục 10.4; không phơi `ipcRenderer` thô.
- CSP bản production: `default-src 'self'; connect-src 'none'; img-src 'self' data:; style-src 'self' 'unsafe-inline'`.
- Bản dev cần thêm `connect-src ws://localhost:5173` cho Vite HMR (HTTP/WebSocket giữa renderer và dev server). Đây là công cụ phát triển, không chở dữ liệu ứng dụng và không có trong bản build (`loadFile`).
- Nhiều tài khoản thật trên một máy: `npm run electron:dev -- --profile=an` đặt `userData` riêng và tên file audit riêng cho từng profile.

### 10.6 Biên dịch và chạy

| Việc | Cách |
|---|---|
| Server C (WSL) | `cd server && make && NETCHAT_DEV=1 ./build/netchat-server` |
| PostgreSQL (Docker) | `docker compose up -d db` |
| Lõi, bản Linux (WSL) | `cd client/native && make` → `build/netchat-core` |
| Lõi, bản Windows | `gcc` của MSYS2 ucrt64: `build.ps1` → `bin/netchat-core.exe` (`-lws2_32 -pthread`); `compat.h` bọc `WSAStartup`, `closesocket`, `SOCKET`, `WSAGetLastError()` |
| Electron | `cd client && npm run electron:dev` (đặt `NETCHAT_CORE` nếu lõi ở chỗ khác) |
| Kết nối Windows ↔ WSL | Lõi Windows kết nối `127.0.0.1:9000`, WSL2 chuyển cổng lắng nghe ra localhost của Windows |

Cây mã nên nằm trong hệ file của WSL (ví dụ `~/net-chat`) khi đo hiệu năng vì truy cập `/mnt/c` chậm hơn.

---

## 11. Server C

### 11.1 Luồng xử lý

- `epoll` level-triggered, một thread, mọi callback chạy trong thread đó nên handler không cần khóa.
- Đọc: `epoll_wait` → `recv` vào buffer của kết nối → lặp: đủ 12 byte header thì đọc `length`/`seq`/`ack`, kiểm tra `1..65536`, đủ `length` byte thì giải mã payload, kiểm tra `seq` (mục 9.3), gọi `msg_dispatch`, dịch buffer; chưa đủ thì chờ lần `recv` sau.
- Ghi: `net_send` chỉ gán `seq`, điền `ack`, đóng khung, mã hóa và đưa vào hàng đợi gửi của kết nối; cuối mỗi vòng lặp epoll mới gọi `send()` một lần cho mỗi kết nối có dữ liệu, bật/tắt `EPOLLOUT` khi `send` trả `EAGAIN`. Nhờ vậy nhiều `EVT_*` sinh ra trong cùng một vòng đi chung một `send()`.
- `recv` trả `0` (client đóng) hoặc `-1` kèm `errno != EAGAIN` (lỗi) đều dẫn tới dọn kết nối: đóng fd, giải phóng bộ nhớ, báo bạn bè và người đang chat.
- Socket được accept với `TCP_NODELAY` và `SO_KEEPALIVE`; `NET_BACKLOG` đủ lớn (1024) cho đợt kết nối đồng loạt.
- Mọi thao tác ghi DB nằm trong transaction ngắn; lỗi DB trả `ERR_INTERNAL`, không làm sập server.

### 11.2 Phân tầng mã

| Tầng | File | Biết gì | Không biết gì |
|---|---|---|---|
| mạng | `net.c`, `common/frame.c`, `common/cipher.c`, `common/audit.c` | socket, epoll, khung, `seq`/`ack`, mã hóa, hàng đợi, timeout, audit | JSON, `type`, user, DB |
| message | `msg.c` | parse JSON, bảng `type → handler`, kiểm tra đăng nhập, rate limit, dựng response/event | socket |
| nghiệp vụ | `auth.c`, các handler, `store.c` | user, phiên, bạn bè, nhóm, DB | khung, mã hóa |

### 11.3 Bộ đếm (trả qua `STATS` và dòng `STAT`)

```
recv_calls  send_calls  frames_in  frames_out  bytes_in  bytes_out  epoll_waits
conns_now   conns_peak  accepts    sendq_peak  gaps      dups       fault_drops   retrans
kicks{login_elsewhere, timeout, server_shutdown, protocol_error}
```

---

## 12. Bot và đo hiệu năng

### 12.1 Bot Python (`tools/bots`)

Bot nói TCP thuần với cùng khung, `seq`/`ack` và mã hóa như lõi; vì là bản cài đặt độc lập nên cũng là bằng chứng đặc tả đủ rõ.

```
python3 tools/bots/run.py --host 127.0.0.1 --users 50 --scenario chat \
        --rate 5 --duration 60 --ramp 50 --seed 1 --out results/chat-50.json
```

Mỗi bot: `HELLO` (chờ `HELLO_RES`) → `REGISTER` (bỏ qua lỗi 6) → `LOGIN` → `PING` theo `ping_ms` → kịch bản. Tên `bot001`, `bot002`..., mật khẩu cố định; `--seed` để lặp lại được; `--ramp` là số bot vào mỗi giây.

| Kịch bản | Hoạt động | Kiểm chứng |
|---|---|---|
| `echo` | Mỗi bot gửi `ECHO` liên tục, đo RTT | Thuần đường mạng: TCP + epoll + khung |
| `ramp` | Tăng 100 → 1000 → 5000 kết nối, mỗi bậc giữ 30 s | Số kết nối đồng thời, CPU và RAM theo kết nối |
| `frag` | Cắt khung ở vị trí ngẫu nhiên (kể cả giữa header); dồn 50 khung vào một `send()` | Xử lý TCP là luồng byte |
| `badframe` | `length` = 0, = 70000; JSON hỏng | `ERR_PROTOCOL` rồi đóng; JSON hỏng thì giữ kết nối |
| `enc` | Kết nối có và không có `HELLO`; gửi khung rõ sau khi đã bật mã hóa | Mã hóa bật/tắt đúng, không lệch pha |
| `loss` | Bật `FAULT` 5–20 % rồi nhắn liên tục | Audit in `FAULT_DROP`, `GAP`, `UNACKED` đúng lúc, ghép log không sót |
| `slow` | Bot không đọc, bot khác đổ tin | Hàng đợi 1 MB: `QUEUE_HIGH`, `QUEUE_FULL`, `LOST_ON_CLOSE`; kết nối khác không bị ảnh hưởng |
| `churn` | Ngắt đột ngột rồi `RESUME` | `CONN_CLOSE` kèm khoảng `seq` nghi mất, `REQ_ABORTED`, không rò fd |
| `chat` | Kết bạn theo vòng, nhắn 1-1 ngẫu nhiên | Luồng chat đầu-cuối |
| `group` | Nhóm 100 người, vài bot nhắn liên tục | Phát tán 1 → N |
| `offline` | Một nửa bot offline, nhắn cho họ, rồi cho đăng nhập | `EVT_OFFLINE_MSGS`, `OFFLINE_ACK` |

### 12.2 Bộ tạo tải C (`tools/loadgen`)

Một thread, một vòng `epoll` (cùng cấu trúc với `net.c`), dùng `common/frame.c` và `common/cipher.c`. Mở N kết nối non-blocking; mỗi kết nối chạy máy trạng thái `HELLO` → `LOGIN` → lặp `ECHO` hoặc `MSG_SEND`; đo RTT bằng `clock_gettime(CLOCK_MONOTONIC)` vào histogram; in kết quả JSON. Có thể chạy nhiều process, mỗi process vài nghìn kết nối.

Quy mô 5.000–10.000 kết nối cần chuẩn bị:

| Giới hạn | Triệu chứng | Chuẩn bị |
|---|---|---|
| `ulimit -n` | `accept`/`socket` lỗi `EMFILE` | `ulimit -n 65535` cho server và bộ tạo tải |
| Cổng nguồn tạm (khoảng 28.000 cho mỗi cặp địa chỉ đích) | `connect` lỗi `EADDRNOTAVAIL` | Dưới 28.000 kết nối thì đủ; nhiều hơn thì dùng nhiều địa chỉ `127.0.0.x` hoặc nhiều cổng |
| `listen` backlog | `ECONNREFUSED` khi vào đồng loạt | `NET_BACKLOG` 1024+, `sysctl net.core.somaxconn`, hoặc vào theo đợt |
| Chưa xác thực quá 30 s bị đóng | Kết nối không `LOGIN` bị đóng | Mọi kết nối của bộ tạo tải đều `LOGIN`; tài khoản tạo sẵn một lần bằng `seed` (REGISTER 50/giây) |
| `LOGIN` ghi DB đồng bộ trong thread epoll | Đợt đăng nhập lớn làm server khựng | Vào theo đợt; thời gian nạp tính riêng, không tính vào số đo ổn định |

### 12.3 Thước đo

| Luận điểm | Thước đo | Nguồn |
|---|---|---|
| Chi phí cho tin nhỏ | byte trên dây ÷ byte nội dung | `bytes_in/out ÷ frames` |
| Kết nối bền, không bắt tay lại mỗi tin | `accepts` so với số tin; RTT trên kết nối đã mở | `STATS`, bot |
| Một thread epoll phục vụ nhiều kết nối | số kết nối đến khi p99 vượt ngưỡng; CPU, RSS theo số kết nối | `ramp`, `pidstat`, `/proc/<pid>/status` |
| Gom gói giảm việc của hệ điều hành | `frames_in ÷ recv_calls`, `frames_out ÷ send_calls`, `ΔOutSegs ÷ frames_out` | `STATS`, `/proc/net/snmp` |
| TCP giữ thứ tự và độ tin cậy khi mất gói | khung đến đủ, `gaps = 0`, `retrans > 0`, p99 tăng | audit `TCP_RETRANS` với `netem` |

Quy tắc đo:

- Độ trễ đầu-cuối chỉ đo khi người gửi và nhận ở cùng một process (`perf_counter_ns` / `CLOCK_MONOTONIC`). Báo **p50, p95, p99, max**, không báo trung bình.
- Mỗi cấu hình bỏ 10 s đầu, đo 60 s, lặp ít nhất 3 lần, báo trung vị.
- Đặt `NETCHAT_RATE_LIMIT=0`, `NETCHAT_AUDIT=off`, chuyển stderr của server ra `/dev/null`. Ghim CPU bằng `taskset` (server 1 nhân, bộ tạo tải các nhân khác).
- Báo cáo hai loại: **"chỉ mạng"** (`ECHO`) và **"đầy đủ"** (có ghi DB); DB ghi đồng bộ trong thread epoll nên che mất chi phí mạng.
- Ghi cấu hình máy: số nhân, RAM cấp cho WSL (`.wslconfig`).

| Kịch bản | Kết nối | Mã hóa | Tin/s | p50 | p99 | Byte/tin | Frame/recv | Frame/send | CPU server | RSS |
|---|---|---|---|---|---|---|---|---|---|---|
| echo | 100 | không | | | | | | | | |
| echo | 100 | caesar | | | | | | | | |
| echo | 1000 | caesar | | | | | | | | |
| echo | 5000 | caesar | | | | | | | | |
| echo | 10000 | caesar | | | | | | | | |
| chat (không DB) | 100 | caesar | | | | | | | | |
| chat (có DB) | 100 | caesar | | | | | | | | |
| chat + `netem` mất 1 % | 100 | caesar | | | | | | | | |

---

## 13. Ví dụ luồng

### 13.1 Kết nối, đăng ký, đăng nhập

```
C→S {"type":"HELLO","id":"k-1","data":{"proto":1,"enc":["caesar"],"ping_ms":1000}}        (seq=1, rõ)
S→C {"type":"HELLO_RES","id":"k-1","ok":true,"data":{"enc":"caesar","ping_ms":1000,"conn_id":17,...}}   (rõ)
--- từ đây mọi payload đã mã hóa ---
C→S {"type":"REGISTER","id":"c-1","data":{"username":"an","password":"123456","display_name":"An"}}
S→C {"type":"REGISTER_RES","id":"c-1","ok":true}
C→S {"type":"LOGIN","id":"c-2","data":{"username":"an","password":"123456"}}
S→C {"type":"LOGIN_RES","id":"c-2","ok":true,"data":{"token":"9f2c...","username":"an","display_name":"An"}}
S→C {"type":"EVT_OFFLINE_MSGS","data":{"messages":[]}}
```

### 13.2 Kết bạn và nhắn tin

```
An   C→S {"type":"FRIEND_REQUEST","id":"c-3","data":{"to":"binh"}}
An   S→C {"type":"FRIEND_REQUEST_RES","id":"c-3","ok":true}
Bình S→C {"type":"EVT_FRIEND_REQUEST","data":{"from":"an","display_name":"An"}}
Bình C→S {"type":"FRIEND_ACCEPT","id":"c-9","data":{"from":"an"}}
Bình S→C {"type":"FRIEND_ACCEPT_RES","id":"c-9","ok":true}
An   S→C {"type":"EVT_FRIEND_ACCEPTED","data":{"username":"binh","display_name":"Bình"}}

An   C→S {"type":"MSG_SEND","id":"c-4","data":{"to":"binh","text":"chào Bình"}}
An   S→C {"type":"MSG_SEND_RES","id":"c-4","ok":true,"data":{"msg_id":42,"ts":1761412345678,"delivered":true}}
Bình S→C {"type":"EVT_MSG","data":{"msg_id":42,"from":"an","text":"chào Bình","ts":1761412345678}}
```

### 13.3 Nhắn khi người nhận offline

```
An   C→S {"type":"MSG_SEND","id":"c-5","data":{"to":"chi","text":"ngủ chưa?"}}
An   S→C {"type":"MSG_SEND_RES","id":"c-5","ok":true,"data":{"msg_id":43,"ts":...,"delivered":false}}
... Chi đăng nhập sau đó ...
Chi  S→C {"type":"EVT_OFFLINE_MSGS","data":{"messages":[{"kind":"direct","msg_id":43,"from":"an","text":"ngủ chưa?","ts":...}]}}
Chi  C→S {"type":"OFFLINE_ACK","id":"c-1","data":{"up_to_msg_id":43}}
Chi  S→C {"type":"OFFLINE_ACK_RES","id":"c-1","ok":true}
```

### 13.4 Nhóm chat

```
An   C→S {"type":"GROUP_CREATE","id":"c-6","data":{"name":"Nhóm LTM"}}
An   S→C {"type":"GROUP_CREATE_RES","id":"c-6","ok":true,"data":{"group_id":3}}
An   C→S {"type":"GROUP_INVITE","id":"c-7","data":{"group_id":3,"username":"binh"}}
Bình S→C {"type":"EVT_GROUP_INVITE","data":{"group_id":3,"name":"Nhóm LTM","from":"an"}}
Bình C→S {"type":"GROUP_ACCEPT","id":"c-10","data":{"group_id":3}}
An   S→C {"type":"EVT_GROUP_JOINED","data":{"group_id":3,"username":"binh"}}
Bình C→S {"type":"GROUP_MSG_SEND","id":"c-11","data":{"group_id":3,"text":"hello cả nhóm"}}
An   S→C {"type":"EVT_GROUP_MSG","data":{"group_id":3,"msg_id":44,"from":"binh","text":"hello cả nhóm","ts":...}}
```

### 13.5 Một bên rớt mạng khi đang chat

```
(An đang CHAT_OPEN với Bình, kết nối của An bị ngắt)
Bình S→C {"type":"EVT_PEER_DISCONNECTED","data":{"username":"an"}}
Bình S→C {"type":"EVT_USER_OFFLINE","data":{"username":"an"}}
```

---

## 14. Thứ tự xây dựng

| Bước | Việc | Xong khi |
|---|---|---|
| 1 | Dựng môi trường: PostgreSQL (Docker), server trong WSL; sửa đường dẫn preload trong `main.cjs` | Server chạy; cửa sổ Electron mở và có `window.netchat` |
| 2 | Tách `common/frame.c` (khung 12 byte, `seq`/`ack`), `cipher.c`, `audit.c`; cập nhật `net.c`, `test_frame.c` | Test khung và mã hóa xanh |
| 3 | `netchat-core` tối thiểu (`connect`, `SEND`, `RECV`, `HELLO`) và `core-link.cjs` | Nút trong giao diện gửi `PING`, nhận `PONG`; log audit hai phía có `FRAME_TX`/`FRAME_RX` khớp `seq` |
| 4 | Audit đầy đủ (mục 9) và `FAULT`; `tools/audit/merge.py`; thẻ "Mạng" | Bật `FAULT`: cả hai bên in `FAULT_DROP` và `GAP` đúng lúc |
| 5 | `REGISTER`/`LOGIN` thật từ giao diện; kết nối lại + `RESUME` | Rút server rồi bật lại, giao diện tự về `open` |
| 6 | Bot Python: `echo`, `frag`, `badframe`, `enc`, `loss` | Mỗi kịch bản chạy đúng như bảng 12.1 |
| 7 | Server làm dần handler: bạn bè → `MSG_SEND` → offline → nhóm; mở dần kịch bản bot tương ứng | Mỗi handler có kịch bản bot xanh |
| 8 | Bộ đếm, `STATS`, bộ tạo tải C, `seed` tài khoản | Giữ 1.000 kết nối `ECHO` trong 60 s, số khung bot gửi = `frames_in` |
| 9 | Đo theo mục 12.3, leo thang tới 10.000 kết nối | Bảng kết quả có số, lặp ≥ 3 lần |
