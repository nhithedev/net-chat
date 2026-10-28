# Đặc tả giao thức ứng dụng Chat (v1)


## 1. Kiến trúc và phạm vi

```
React (trình duyệt) --WebSocket--> Gateway (Node.js) --TCP--> Server C (epoll)
```

- **Server C** nói TCP thuần, dùng framing có tiền tố độ dài (mục 2).
- **Gateway** chỉ chuyển byte, không parse hay sửa JSON. Mỗi WebSocket tương ứng với đúng một kết nối TCP.
- Gateway nhận/gửi WebSocket **text frame**, mỗi frame chứa đúng một JSON (không có tiền tố độ dài). Gateway thêm/bỏ tiền tố 4 byte khi chuyển sang/từ TCP.
- Cổng mặc định: Server TCP `9000`, Gateway WebSocket `8080`.

---

## 2. Framing (khung dữ liệu trên TCP)

TCP là luồng byte, không có ranh giới message, nên mỗi message được đóng khung:

```
+----------------------+-------------------------------+
| length (4 byte)      | payload (length byte, UTF-8)  |
| uint32, big-endian   | JSON                          |
+----------------------+-------------------------------+
```

- `length` là độ dài **chỉ của payload** (không tính 4 byte header), mã hóa big-endian (`htonl`/`ntohl`).
- Payload là một đối tượng JSON mã hóa UTF-8, không có ký tự NUL.
- Người nhận phải tự ghép/tách:
  - Một lần `recv` có thể chứa **nhiều** message liền nhau.
  - Một message có thể bị **chia** qua nhiều lần `recv`, kể cả header bị cắt (ví dụ mới nhận 2/4 byte header).
- **Giới hạn**: `length` phải thỏa `1 <= length <= 65536`. Vi phạm (bằng 0 hoặc quá lớn) coi là lỗi giao thức: gửi `ERR_PROTOCOL` (nếu còn gửi được) rồi **đóng kết nối**, vì không thể đồng bộ lại ranh giới message.

Ví dụ: payload `{"type":"PING"}` dài 15 byte, nên trên dây là:

```
00 00 00 0F 7B 22 74 79 70 65 22 3A 22 50 49 4E 47 22 7D
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
| `id` | có | Chuỗi do client tự sinh (ví dụ `c-1`, `c-2`...), duy nhất trong một kết nối. Server trả lại nguyên trong response để client ghép cặp. |
| `data` | tùy lệnh | Đối tượng tham số. |

### 3.2 Response (server → client, trả lời một request)

```json
{ "type": "LOGIN_RES", "id": "c-17", "ok": true, "data": { "token": "..." } }
{ "type": "LOGIN_RES", "id": "c-17", "ok": false, "error": { "code": 1002, "message": "Sai mật khẩu" } }
```

- `type` = tên request + `_RES`.
- `id` = `id` của request tương ứng.
- `ok` = `true`/`false`. Nếu `false` thì có `error`, ngược lại có `data` (có thể vắng nếu không có gì trả về).

### 3.3 Event (server → client, tự đẩy, không ứng với request)

```json
{ "type": "EVT_MSG", "data": { "msg_id": 42, "from": "an", "text": "hi", "ts": 1761412345678 } }
```

- `type` luôn bắt đầu bằng `EVT_`.
- Không có `id` và không có `ok`.

### 3.4 Quy ước chung

- Tên trường JSON dùng `snake_case`. Tên `type` dùng `UPPER_SNAKE_CASE`.
- Thời gian (`ts`) là **Unix epoch tính bằng mili-giây** (số nguyên).
- `msg_id`, `group_id` là số nguyên do server cấp, tăng dần (an toàn với JavaScript vì < 2^53).
- Người dùng được định danh bằng `username` (duy nhất, phân biệt hoa/thường không áp dụng: server lưu chữ thường).
- Server **bỏ qua** trường lạ trong `data` (để dễ mở rộng); thiếu trường bắt buộc hoặc sai kiểu thì trả `ERR_BAD_REQUEST` (code 1).
- `text` tối đa 4000 byte UTF-8.

---

## 4. Phiên làm việc (session)

### 4.1 Trạng thái kết nối

```
CONNECTED ──LOGIN/RESUME ok──> AUTHENTICATED ──LOGOUT / đóng / timeout──> CLOSED
    │
    └── 30 s không đăng nhập ──> CLOSED
```

- Ngay sau khi kết nối, client ở trạng thái `CONNECTED`. Chỉ được gửi `REGISTER`, `LOGIN`, `RESUME`, `PING`. Lệnh khác trả `ERR_NOT_AUTHENTICATED` (code 2).
- Kết nối chưa xác thực quá **30 giây** sẽ bị server đóng.

### 4.2 Token và khôi phục phiên

- `LOGIN` thành công trả về `token` (chuỗi ngẫu nhiên 32 byte hex), gắn với `username`.
- Khi mất kết nối đột ngột (rớt mạng, F5 trang), phiên được **giữ 60 giây**. Trong thời gian đó client mở kết nối mới và gửi `RESUME` với `token` để vào lại mà không cần mật khẩu.
- `LOGOUT` hoặc hết 60 giây thì token bị hủy.
- Trạng thái online/offline của user trong danh sách bạn bè tính theo **kết nối đang mở**, không tính theo thời gian giữ phiên.

### 4.3 Đăng nhập trùng

Mỗi tài khoản chỉ có **một kết nối** hoạt động. Khi đăng nhập từ nơi thứ hai:
- Kết nối cũ nhận `EVT_KICKED` với `reason = "login_elsewhere"` rồi bị đóng.
- Kết nối mới đăng nhập bình thường.

### 4.4 Heartbeat và timeout

- Mỗi bên có thể gửi `PING`; bên kia phải trả `PONG` ngay.
- Client nên gửi `PING` mỗi **20 giây** khi rảnh.
- Server đóng kết nối nếu **60 giây** không nhận được bất kỳ byte nào từ client.
- Khi server đóng một kết nối đã xác thực, server đẩy `EVT_USER_OFFLINE` cho các bạn bè đang online và `EVT_PEER_DISCONNECTED` cho những người đang mở chat 1-1 với user đó.

---

## 5. Danh sách message

Ký hiệu: **C→S** request của client, **S→C** response hoặc event. Mọi request đều có response `<TYPE>_RES` với `ok`/`error`; bảng chỉ liệt kê `data` của request và `data` khi thành công.

### 5.1 Tài khoản

| Type | Yêu cầu đăng nhập | `data` request | `data` response (ok) |
|---|---|---|---|
| `REGISTER` | không | `username` (3–20 ký tự `a-z 0-9 _`), `password` (6–64 ký tự), `display_name` (1–30 ký tự) | `{}` |
| `LOGIN` | không | `username`, `password` | `token`, `username`, `display_name` |
| `RESUME` | không | `token` | `username`, `display_name` |
| `LOGOUT` | có | — | `{}` (sau đó server đóng kết nối) |
| `CHANGE_PASSWORD` | có | `old_password`, `new_password` | `{}` |
| `UPDATE_PROFILE` | có | `display_name` | `display_name` |

Ghi chú: mật khẩu gửi lên là bản rõ trong JSON; server **băm (SHA-256 + salt ngẫu nhiên theo từng user)** trước khi lưu, không bao giờ lưu mật khẩu thô. (Nâng cấp sau: TLS.)

### 5.2 Hiện diện (presence)

| Type | `data` request | `data` response (ok) |
|---|---|---|
| `ONLINE_LIST` | — | `users`: mảng `{ "username", "display_name" }` của mọi user đang online |

Event:

| Type | Khi nào | `data` |
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
| `FRIEND_LIST` | — | `friends`: mảng `{ "username", "display_name", "online" (bool) }`; `pending_in`: mảng username đã mời mình; `pending_out`: mảng username mình đã mời |

Event:

| Type | Gửi cho | `data` |
|---|---|---|
| `EVT_FRIEND_REQUEST` | người nhận lời mời | `from`, `display_name` |
| `EVT_FRIEND_ACCEPTED` | người đã mời | `username`, `display_name` |
| `EVT_FRIEND_REJECTED` | người đã mời | `username` |
| `EVT_FRIEND_REMOVED` | người bị hủy kết bạn | `username` |

Quy tắc: không tự kết bạn với chính mình (`ERR_BAD_REQUEST`); đã là bạn thì trả `ERR_ALREADY_EXISTS` (code 6); lời mời gửi lúc người kia offline vẫn được lưu và hiện trong `pending_in` khi họ đăng nhập.

### 5.4 Chat 1-1

Chỉ được nhắn cho **bạn bè**; người lạ trả `ERR_NOT_FRIENDS` (code 4).

| Type | `data` request | `data` response (ok) |
|---|---|---|
| `MSG_SEND` | `to` (username), `text` | `msg_id`, `ts`, `delivered` (bool: `true` nếu người nhận đang online và đã được đẩy tin; `false` nếu đã lưu offline) |
| `CHAT_OPEN` | `with` (username) | `history`: tối đa 50 tin gần nhất `{ "msg_id","from","text","ts" }`, sắp theo thời gian tăng dần |
| `CHAT_CLOSE` | `with` | `{}` |

`CHAT_OPEN` dùng khi người dùng chuyển sang một cuộc trò chuyện khác; client gọi lại với người mới. `CHAT_CLOSE` là yêu cầu **dừng cuộc trò chuyện**.

Event:

| Type | Gửi cho | `data` |
|---|---|---|
| `EVT_MSG` | người nhận | `msg_id`, `from`, `text`, `ts` |
| `EVT_CHAT_CLOSED` | bên còn lại, khi một bên gửi `CHAT_CLOSE` | `username` |
| `EVT_PEER_DISCONNECTED` | bên còn lại, khi một bên ngắt kết nối hoặc rớt mạng | `username` |

### 5.5 Nhóm chat

Chỉ **thành viên** mới gửi tin/xem được nhóm; không phải thành viên trả `ERR_NOT_MEMBER` (code 5). Người tạo nhóm là `owner`.

| Type | Ai được gọi | `data` request | `data` response (ok) |
|---|---|---|---|
| `GROUP_CREATE` | mọi user | `name` (1–40 ký tự) | `group_id` |
| `GROUP_INVITE` | thành viên nhóm | `group_id`, `username` (phải là bạn bè của người mời) | `{}` |
| `GROUP_ACCEPT` | người được mời | `group_id` | `members` |
| `GROUP_DECLINE` | người được mời | `group_id` | `{}` |
| `GROUP_KICK` | **owner** | `group_id`, `username` | `{}` |
| `GROUP_LEAVE` | thành viên | `group_id` | `{}` |
| `GROUP_LIST` | mọi user | — | `groups`: mảng `{ "group_id","name","owner" }`; `invites`: mảng `{ "group_id","name","from" }` |
| `GROUP_MEMBERS` | thành viên | `group_id` | `members`: mảng `{ "username","display_name","online" }` |
| `GROUP_MSG_SEND` | thành viên | `group_id`, `text` | `msg_id`, `ts` |
| `GROUP_HISTORY` | thành viên | `group_id` | `history`: tối đa 50 tin gần nhất `{ "msg_id","from","text","ts" }` |

Ghi chú: nếu `owner` rời nhóm thì quyền owner chuyển cho thành viên vào nhóm sớm nhất; nhóm hết người thì bị xóa.

Event:

| Type | Gửi cho | `data` |
|---|---|---|
| `EVT_GROUP_INVITE` | người được mời | `group_id`, `name`, `from` |
| `EVT_GROUP_JOINED` | các thành viên hiện có | `group_id`, `username` |
| `EVT_GROUP_LEFT` | các thành viên còn lại | `group_id`, `username` |
| `EVT_GROUP_KICKED` | người bị đuổi (và các thành viên còn lại nhận `EVT_GROUP_LEFT`) | `group_id` |
| `EVT_GROUP_MSG` | mọi thành viên **trừ người gửi** | `group_id`, `msg_id`, `from`, `text`, `ts` |

### 5.6 Tin nhắn offline

- Khi `MSG_SEND`/`GROUP_MSG_SEND` mà người nhận **không có kết nối**, server lưu tin vào DB với trạng thái chưa giao.
- Ngay sau khi `LOGIN`/`RESUME` thành công, server đẩy các tin chưa giao bằng một event duy nhất:

```json
{ "type": "EVT_OFFLINE_MSGS", "data": { "messages": [
  { "kind": "direct", "msg_id": 40, "from": "binh", "text": "alo", "ts": 1761410000000 },
  { "kind": "group",  "msg_id": 41, "group_id": 3, "from": "chi", "text": "họp nhé", "ts": 1761410100000 }
] } }
```

- Mỗi batch tối đa 100 tin, sắp theo `msg_id` tăng dần (cùng thứ tự với `ts` vì `msg_id` do server cấp tăng dần; dùng `msg_id` để `up_to_msg_id` của `OFFLINE_ACK` luôn đúng).
- Client xử lý xong thì gửi `OFFLINE_ACK` để server đánh dấu đã giao:

| Type | `data` request | `data` response (ok) |
|---|---|---|
| `OFFLINE_ACK` | `up_to_msg_id` (số lớn nhất client đã nhận trong batch) | `{}` |

- Nếu còn tin chưa giao (batch bị cắt ở 100), sau `OFFLINE_ACK` server đẩy tiếp batch kế. Nếu client rớt trước khi gửi ACK, tin **vẫn ở trạng thái chưa giao** và sẽ được gửi lại lần đăng nhập sau (giao tối thiểu một lần; client khử trùng theo `msg_id`).

### 5.7 Hệ thống

| Type | Hướng | Ghi chú |
|---|---|---|
| `PING` | C→S hoặc S→C | `data` không bắt buộc |
| `PONG` | phản hồi của `PING` | giữ nguyên `id` nếu `PING` có `id` |
| `EVT_KICKED` | S→C | `data.reason`: `login_elsewhere` \| `timeout` \| `server_shutdown` \| `protocol_error` |

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
| 10 | `ERR_PROTOCOL` | Lỗi framing; sau lỗi này kết nối bị đóng |
| 11 | `ERR_INTERNAL` | Lỗi phía server (DB, hết bộ nhớ...) |
| 1001 | `ERR_USER_NOT_FOUND` | `LOGIN` với username không tồn tại |
| 1002 | `ERR_WRONG_PASSWORD` | Sai mật khẩu |
| 1003 | `ERR_TOKEN_INVALID` | `RESUME` với token sai hoặc đã hết hạn |

Quy ước: mã **1–99** là lỗi chung, mã **1000+** là lỗi riêng của nhóm tài khoản. `message` là chuỗi mô tả cho người đọc, client không nên dựa vào nội dung của nó mà dựa vào `code`.

---

## 7. Giới hạn

| Mục | Giá trị |
|---|---|
| Kích thước payload tối đa | 65536 byte |
| `text` tối đa | 4000 byte |
| Thời gian chờ đăng nhập | 30 giây |
| Thời gian không hoạt động tối đa | 60 giây |
| Thời gian giữ phiên sau khi rớt | 60 giây |
| Tốc độ gửi tối đa mỗi kết nối | 20 message/giây (vượt thì `ERR_RATE_LIMIT`) |
| Hàng đợi gửi tối đa mỗi kết nối | 1 MB; vượt thì server đóng kết nối (client đọc quá chậm), `EVT_KICKED` với `reason = "timeout"` |
| Số thành viên tối đa một nhóm | 100 |
| Số tin trong `history` | 50 |

---

## 8. Ví dụ luồng

### 8.1 Đăng ký và đăng nhập

```
C→S {"type":"REGISTER","id":"c-1","data":{"username":"an","password":"123456","display_name":"An"}}
S→C {"type":"REGISTER_RES","id":"c-1","ok":true}
C→S {"type":"LOGIN","id":"c-2","data":{"username":"an","password":"123456"}}
S→C {"type":"LOGIN_RES","id":"c-2","ok":true,"data":{"token":"9f2c...","username":"an","display_name":"An"}}
S→C {"type":"EVT_OFFLINE_MSGS","data":{"messages":[]}}
```

### 8.2 Kết bạn và nhắn tin

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

### 8.3 Nhắn khi người nhận offline

```
An   C→S {"type":"MSG_SEND","id":"c-5","data":{"to":"chi","text":"ngủ chưa?"}}
An   S→C {"type":"MSG_SEND_RES","id":"c-5","ok":true,"data":{"msg_id":43,"ts":...,"delivered":false}}
... Chi đăng nhập sau đó ...
Chi  S→C {"type":"LOGIN_RES",...}
Chi  S→C {"type":"EVT_OFFLINE_MSGS","data":{"messages":[{"kind":"direct","msg_id":43,"from":"an","text":"ngủ chưa?","ts":...}]}}
Chi  C→S {"type":"OFFLINE_ACK","id":"c-1","data":{"up_to_msg_id":43}}
Chi  S→C {"type":"OFFLINE_ACK_RES","id":"c-1","ok":true}
```

### 8.4 Nhóm chat

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

### 8.5 Một bên rớt mạng khi đang chat

```
(An đang CHAT_OPEN với Bình, kết nối của An bị ngắt)
Bình S→C {"type":"EVT_PEER_DISCONNECTED","data":{"username":"an"}}
Bình S→C {"type":"EVT_USER_OFFLINE","data":{"username":"an"}}
```

---

## 9. Ghi chú triển khai cho server C

- Đọc: `epoll` → `recv` vào buffer của kết nối → lặp: nếu đủ 4 byte thì đọc `length`, kiểm tra `1..65536`, nếu đủ `length` byte payload thì parse JSON và gọi handler, rồi dịch buffer; nếu chưa đủ thì chờ lần `recv` sau.
- Ghi: `net_send` chỉ đưa frame (header + payload) vào hàng đợi gửi của kết nối; vòng lặp epoll lo `send` từng phần và bật/tắt `EPOLLOUT`.
- Luôn xử lý `recv` trả `0` (client đóng) và `-1` kèm `errno != EAGAIN` (lỗi) bằng cách dọn kết nối.
- Mọi thao tác ghi DB nên gói trong transaction ngắn; log mỗi sự kiện quan trọng (kết nối, đăng nhập, ngắt, lỗi giao thức) kèm `conn_id` và thời gian.

## 10. Lịch sử thay đổi

| Ngày | Thay đổi | Người sửa |
|---|---|---|
| 2026-10-26 | Bản nháp đầu tiên (v1) | |
| 2026-10-28 | Batch tin offline sắp theo `msg_id` thay vì `ts` | |
