# Chức năng của hệ thống Chat và cách thực hiện

Ứng dụng chat nhiều người dùng cho bài tập lớn Lập trình mạng. Tài liệu này nêu **hệ thống làm gì** và **mỗi chức năng được thực hiện bằng message nào, ở đâu**. Khung dữ liệu, cấu trúc message, mã lỗi, audit log và lõi mạng C nằm ở [protocol-v2.md](protocol-v2.md).

## 1. Kiến trúc tóm tắt

```
React (renderer) ──IPC──> Electron main ──pipe──> netchat-core (C) ──TCP──> Server C (epoll) ──> PostgreSQL
```

| Thành phần | Việc chính |
|---|---|
| Giao diện React | Hiển thị, giữ trạng thái, gọi `window.netchat.request(...)`, nhận `EVT_*` |
| Electron main | Chạy `netchat-core`, chuyển tiếp, ghép `id` request với `Promise` |
| `netchat-core` (C) | Toàn bộ phía mạng của client: socket, khung, `seq`/`ack`, mã hóa, heartbeat, kết nối lại, audit |
| Server C (epoll) | Khung, giải mã, nghiệp vụ, phiên, định tuyến tin, rate limit, ghi DB, audit log |
| PostgreSQL (Docker) | User, phiên, bạn bè, nhóm, tin nhắn, trạng thái giao |

Trọng tâm là **lập trình mạng**: xử lý luồng TCP, cơ chế I/O của server, quản lý phiên, xử lý ngắt kết nối, và quan sát được đường truyền bằng audit log.

## 2. Bảng tổng hợp

Mức: **P0** = bắt buộc; **P1** = hỗ trợ trình bày và đo.

| # | Chức năng | Message chính | Mức |
|---|---|---|---|
| 1 | Đăng ký tài khoản | `REGISTER` | P0 |
| 2 | Đăng nhập, khôi phục phiên | `LOGIN`, `RESUME` | P0 |
| 3 | Đăng xuất | `LOGOUT` | P0 |
| 4 | Xem danh sách người dùng online | `ONLINE_LIST`, `EVT_USER_ONLINE/OFFLINE` | P0 |
| 5 | Gửi lời mời kết bạn | `FRIEND_REQUEST`, `EVT_FRIEND_REQUEST` | P0 |
| 6 | Chấp nhận / từ chối kết bạn | `FRIEND_ACCEPT`, `FRIEND_REJECT`, `EVT_FRIEND_ACCEPTED/REJECTED` | P0 |
| 7 | Hủy kết bạn | `FRIEND_REMOVE`, `EVT_FRIEND_REMOVED` | P0 |
| 8 | Danh sách bạn bè và trạng thái | `FRIEND_LIST` | P0 |
| 9 | Gửi tin nhắn 1-1 | `MSG_SEND`, `EVT_MSG`, `CHAT_OPEN` | P0 |
| 10 | Ngắt kết nối / dừng chat | `CHAT_CLOSE`, `EVT_CHAT_CLOSED`, `EVT_PEER_DISCONNECTED`, `LOGOUT`, đóng socket | P0 |
| 11 | Tạo nhóm chat | `GROUP_CREATE` | P0 |
| 12 | Thêm người vào nhóm | `GROUP_INVITE`, `GROUP_ACCEPT/DECLINE`, `EVT_GROUP_INVITE/JOINED` | P0 |
| 13 | Xóa người khỏi nhóm | `GROUP_KICK`, `EVT_GROUP_KICKED/LEFT` | P0 |
| 14 | Rời nhóm | `GROUP_LEAVE`, `EVT_GROUP_LEFT` | P0 |
| 15 | Gửi tin vào nhóm | `GROUP_MSG_SEND`, `EVT_GROUP_MSG`, `GROUP_HISTORY` | P0 |
| 16 | Tin nhắn offline | `MSG_SEND`/`GROUP_MSG_SEND` + `EVT_OFFLINE_MSGS`, `OFFLINE_ACK` | P0 |
| 17 | Giao diện đồ họa | toàn bộ message, qua `window.netchat` | P0 |
| 18 | Mã hóa đường truyền | `HELLO` | P0 |
| 19 | Audit log mạng (client và server) | không có message riêng; `seq`/`ack` trong khung, `PING`/`PONG` | P0 |
| 20 | Bot nhiều tài khoản, bộ tạo tải, đo hiệu năng | dùng toàn bộ giao thức, thêm `ECHO`, `STATS`, `FAULT` (dev) | P1 |

---

## 3. Cách thực hiện từng chức năng

Quy ước: **C→S** là request, **S→C** là response hoặc event. Mọi request có `id` và response `<TYPE>_RES` mang lại `id` đó. Các bước "Lõi" là việc của `netchat-core`, "Server" là việc của Server C; giao diện chỉ gọi `window.netchat`.

### 3.1 Đăng ký tài khoản

1. Giao diện gọi `request('REGISTER', {username, password, display_name})` khi kết nối ở trạng thái `CONNECTED`.
2. Server kiểm tra: `username` 3–20 ký tự `a-z 0-9 _` (đổi sang chữ thường), `password` 6–64, `display_name` 1–30. Sai thì `ERR_BAD_REQUEST`.
3. Sinh salt ngẫu nhiên, tính SHA-256(salt + password), `INSERT` vào bảng user trong một transaction ngắn.
4. Trùng username thì `ERR_ALREADY_EXISTS` (6); thành công thì `REGISTER_RES ok`.
5. Giao diện: form đăng ký, hiển thị lỗi theo `error.code`; đăng ký xong có thể tự đăng nhập.

### 3.2 Đăng nhập và khôi phục phiên

1. Ngay khi kết nối, lõi gửi `HELLO` một mình, chờ `HELLO_RES`, rồi bật mã hóa (chức năng 18).
2. C→S `LOGIN` với `username`, `password`.
3. Server tìm user: không có thì `ERR_USER_NOT_FOUND` (1001); băm mật khẩu với salt đã lưu, sai thì `ERR_WRONG_PASSWORD` (1002).
4. Tài khoản đã có kết nối khác thì kết nối cũ nhận `EVT_KICKED` (`login_elsewhere`) rồi bị đóng.
5. Sinh `token` 32 byte hex, lưu cùng hạn, chuyển kết nối sang `AUTHENTICATED`, trả `LOGIN_RES` (`token`, `username`, `display_name`).
6. Server báo `EVT_USER_ONLINE` cho bạn bè đang online, rồi đẩy `EVT_OFFLINE_MSGS` (3.16).
7. Main đưa `token` cho lõi (`set_session`). Mất kết nối trong 60 giây, lõi tự kết nối lại, gửi `HELLO` rồi `RESUME` bằng `token`; sai hoặc hết hạn thì `ERR_TOKEN_INVALID` (1003) và giao diện về màn đăng nhập.
8. Chưa đăng nhập trong 30 giây thì server đóng kết nối.

### 3.3 Đăng xuất

1. C→S `LOGOUT`.
2. Server hủy `token`, trả `LOGOUT_RES ok`, rồi đóng kết nối TCP.
3. Server báo `EVT_USER_OFFLINE` cho bạn bè và `EVT_PEER_DISCONNECTED` cho người đang chat 1-1 với user này.
4. Main xóa `token` ở lõi, giao diện về màn đăng nhập; lõi không tự `RESUME` sau đăng xuất.

### 3.4 Xem danh sách người dùng online

- C→S `ONLINE_LIST`: server trả `users` `{username, display_name}` của mọi kết nối đã xác thực, dựng từ bảng kết nối trong bộ nhớ (không truy vấn DB).
- Cập nhật thời gian thực bằng `EVT_USER_ONLINE` / `EVT_USER_OFFLINE`; hai event này gửi cho **bạn bè**. Muốn thấy thay đổi của người chưa là bạn thì gọi lại `ONLINE_LIST`.
- Giao diện gọi `ONLINE_LIST` sau khi đăng nhập, dùng để chọn người gửi lời mời kết bạn.
- Phạm vi: danh sách là người **đang online**, không liệt kê mọi tài khoản đã đăng ký.

### 3.5 Gửi lời mời kết bạn

1. C→S `FRIEND_REQUEST` với `to`.
2. Server kiểm tra: `to` là chính mình thì `ERR_BAD_REQUEST`; không tồn tại thì `ERR_NOT_FOUND`; đã là bạn hoặc đã mời thì `ERR_ALREADY_EXISTS`.
3. Ghi quan hệ `pending` (người mời → người nhận) vào DB.
4. Người nhận online thì nhận `EVT_FRIEND_REQUEST` (`from`, `display_name`). Offline thì lời mời nằm trong DB và hiện ở `pending_in` của `FRIEND_LIST` khi họ đăng nhập.
5. Giao diện: nút "Kết bạn" cạnh người dùng trong danh sách online; thông báo khi nhận lời mời.

### 3.6 Chấp nhận / từ chối kết bạn

- Chấp nhận: C→S `FRIEND_ACCEPT` (`from`). Server đổi `pending` thành `accepted`, trả `ok`, đẩy `EVT_FRIEND_ACCEPTED` cho người đã mời.
- Từ chối: C→S `FRIEND_REJECT` (`from`). Server xóa lời mời, đẩy `EVT_FRIEND_REJECTED`.
- Không có lời mời tương ứng thì `ERR_NOT_FOUND`.
- Sau khi chấp nhận, hai bên nhận `EVT_USER_ONLINE/OFFLINE` của nhau; giao diện gọi lại `FRIEND_LIST` hoặc tự cập nhật danh sách.

### 3.7 Hủy kết bạn

1. C→S `FRIEND_REMOVE` (`username`).
2. Server xóa quan hệ (không phải bạn thì `ERR_NOT_FOUND`), trả `ok`, đẩy `EVT_FRIEND_REMOVED` cho bên kia nếu online.
3. Từ lúc này `MSG_SEND` giữa hai người trả `ERR_NOT_FRIENDS`; lịch sử cũ giữ nguyên trong DB.

### 3.8 Danh sách bạn bè và trạng thái

1. C→S `FRIEND_LIST` (không tham số).
2. Server trả `friends` `{username, display_name, online}`, `pending_in`, `pending_out`. `online` lấy từ bảng kết nối đang mở.
3. Gọi sau khi đăng nhập hoặc `RESUME`; sau đó giữ trạng thái bằng `EVT_USER_ONLINE/OFFLINE`, không cần hỏi lại định kỳ.

### 3.9 Gửi tin nhắn 1-1

1. Chọn bạn: C→S `CHAT_OPEN` (`with`); server trả `history` (tối đa 50 tin, cũ đến mới). Giao diện giữ lịch sử đã tải trong bộ nhớ nên chuyển qua lại không mất tin.
2. Gửi: C→S `MSG_SEND` (`to`, `text` ≤ 4000 byte).
3. Server kiểm tra: không phải bạn thì `ERR_NOT_FRIENDS`; quá 4000 byte thì `ERR_TOO_LARGE`; quá 20 message/giây thì `ERR_RATE_LIMIT`.
4. Server lưu tin vào DB lấy `msg_id` tăng dần. Người nhận online thì đẩy `EVT_MSG` và trả `delivered: true`; không thì lưu chưa giao, trả `delivered: false` (3.16).
5. Giao diện: hiển thị tin của mình ngay khi có response, đánh dấu "đã giao / chưa giao" theo `delivered`; tin mới tự cuộn xuống.

### 3.10 Ngắt kết nối

| Tình huống | Cách thực hiện |
|---|---|
| Dừng một cuộc trò chuyện 1-1 | C→S `CHAT_CLOSE` (`with`); bên kia nhận `EVT_CHAT_CLOSED`. Kết nối TCP vẫn mở. |
| Người dùng thoát hẳn | `LOGOUT` (3.3). |
| Rớt mạng / tắt ứng dụng | Server thấy `recv` trả 0 hoặc lỗi, dọn fd, giữ phiên 60 giây, báo `EVT_PEER_DISCONNECTED` cho người đang chat 1-1 và `EVT_USER_OFFLINE` cho bạn bè. Audit ghi `CONN_CLOSE`, khoảng `seq` nghi mất và `REQ_ABORTED` (3.19). |
| Im lặng quá 60 giây | Server gửi `EVT_KICKED` (`timeout`) rồi đóng. Lõi tránh bằng `PING` theo chu kỳ, server trả `PONG`. |

Khi kết nối đóng, lõi báo `STATUS reconnecting` và thử `RESUME` trong 60 giây; giao diện hiện trạng thái kết nối tương ứng.

### 3.11 Tạo nhóm chat

1. C→S `GROUP_CREATE` (`name` 1–40 ký tự).
2. Server tạo nhóm, thêm người tạo làm thành viên vai trò `owner`, trả `group_id`.
3. Giao diện cập nhật danh sách nhóm.

### 3.12 Thêm người vào nhóm

Thêm theo kiểu **mời rồi chấp nhận**:

1. Thành viên bất kỳ: C→S `GROUP_INVITE` (`group_id`, `username`). Server kiểm tra người gọi là thành viên (`ERR_NOT_MEMBER`), người được mời là **bạn của người mời** (`ERR_NOT_FRIENDS`), chưa là thành viên (`ERR_ALREADY_EXISTS`), nhóm chưa đủ 100 người.
2. Server lưu lời mời, đẩy `EVT_GROUP_INVITE` cho người được mời. Offline thì lời mời hiện trong `invites` của `GROUP_LIST` khi đăng nhập.
3. Người được mời: `GROUP_ACCEPT` (nhận `members`; server đẩy `EVT_GROUP_JOINED` cho thành viên hiện có) hoặc `GROUP_DECLINE`.
4. Vào nhóm xong, giao diện gọi `GROUP_HISTORY` lấy 50 tin gần nhất.

### 3.13 Xóa người khỏi nhóm

1. C→S `GROUP_KICK` (`group_id`, `username`). Chỉ **owner** được gọi, người khác nhận `ERR_PERMISSION`.
2. Server xóa thành viên, đẩy `EVT_GROUP_KICKED` cho người bị đuổi và `EVT_GROUP_LEFT` cho các thành viên còn lại.
3. Người bị đuổi không còn gửi/xem được nhóm (`ERR_NOT_MEMBER`); giao diện của họ xóa nhóm khỏi danh sách và hiện thông báo.

### 3.14 Rời nhóm

1. C→S `GROUP_LEAVE` (`group_id`).
2. Server xóa người đó khỏi nhóm và đẩy `EVT_GROUP_LEFT` cho thành viên còn lại.
3. Người rời là owner thì quyền chuyển cho thành viên vào nhóm sớm nhất; nhóm hết người thì xóa.

### 3.15 Gửi tin vào nhóm

1. C→S `GROUP_MSG_SEND` (`group_id`, `text`). Không phải thành viên thì `ERR_NOT_MEMBER`.
2. Server lưu tin, lấy `msg_id`, trả `msg_id`, `ts`.
3. Server đẩy `EVT_GROUP_MSG` cho mọi thành viên đang online **trừ người gửi**. Thành viên offline được lưu chưa giao theo từng người (3.16), nên cần bảng trạng thái giao theo cặp (tin, người nhận).
4. Lịch sử: `GROUP_HISTORY` trả 50 tin gần nhất.

### 3.16 Tin nhắn offline

Không có lệnh riêng: người gửi dùng `MSG_SEND` / `GROUP_MSG_SEND`, server tự quyết định theo việc người nhận có kết nối hay không.

1. Người nhận không có kết nối: server lưu tin chưa giao; response 1-1 có `delivered: false`.
2. Khi người nhận `LOGIN`/`RESUME` thành công, server đẩy `EVT_OFFLINE_MSGS` gồm tối đa 100 tin sắp theo `msg_id` tăng dần; mỗi tin có `kind` là `direct` hoặc `group`.
3. Giao diện hiển thị, khử trùng theo `msg_id`, rồi gửi `OFFLINE_ACK` với `up_to_msg_id` lớn nhất đã nhận.
4. Server đánh dấu đã giao các tin **của riêng người này** có `msg_id` ≤ mốc trong batch vừa gửi (vì `msg_id` của tin 1-1 và tin nhóm cùng tăng trên một dãy), rồi đẩy batch kế nếu còn.
5. Rớt mạng trước khi ACK thì tin vẫn chưa giao và được gửi lại lần đăng nhập sau (giao ít nhất một lần).

### 3.17 Giao diện đồ họa

React chạy trong Electron, nói chuyện với phía mạng chỉ qua `window.netchat` (xem protocol-v2.md, mục 10.4):

- **Lớp kết nối:** `request(type, data)` trả `Promise` theo `id`; `onEvent` nhận `EVT_*`; `onStatus` nhận `connecting | open | reconnecting | restored | closed`; `onAudit` nhận dòng audit.
- **Store:** `me`, `friends`, `pending_in/out`, `groups`, `invites`, `conversations[key] = {messages, loaded}` (khóa `dm:<username>` hoặc `group:<id>`), `connectionState`.
- **Màn hình:** đăng nhập/đăng ký; bố cục thanh bên (bạn bè có chấm online, nhóm, lời mời) và khung chat; thanh trạng thái kết nối; toast cho `EVT_FRIEND_REQUEST`, `EVT_GROUP_INVITE`, `EVT_KICKED`, `EVT_GROUP_KICKED`, `EVT_PEER_DISCONNECTED`.
- **Panel audit:** thẻ "Mạng" (3.19) hiện audit log theo thời gian thực, có nút bật mô phỏng mất gói.
- **Lỗi:** ánh xạ `error.code` sang thông báo tiếng Việt, không dựa vào `error.message`.
- **Bảo mật renderer:** không có quyền ra mạng (CSP `connect-src 'none'` ở bản build).

### 3.18 Mã hóa đường truyền

1. Sau khi kết nối, lõi gửi `HELLO` (rõ) với danh sách `enc` hỗ trợ và `ping_ms`; server trả `HELLO_RES` (rõ) với `enc` được chọn.
2. Từ khung kế tiếp, hai bên mã hóa/giải mã payload bằng phép dịch byte (Caesar, khóa cố định 3).
3. Server giải mã ngay sau khi ghép đủ khung, trước khi parse JSON; mã hóa ngay trước khi đưa vào hàng đợi gửi. Tầng nghiệp vụ không biết có mã hóa.
4. Mật khẩu đi trong payload đã mã hóa; server vẫn chỉ lưu SHA-256 + salt.
5. Đây là phép che dữ liệu đơn giản, không phải bảo mật; chi tiết giới hạn ở protocol-v2.md, mục 8.

### 3.19 Audit log mạng

Mục đích: **nhìn thấy ngay lúc một gói bị mất**, ở cả client và server, cùng một định dạng để đối chiếu.

1. Mọi khung có `seq` (số thứ tự gửi) và `ack` (số thứ tự lớn nhất đã nhận); tầng mạng tự điền, nghiệp vụ không can thiệp.
2. Bên nhận kiểm tra `seq` mỗi khung:
   - liên tục → bình thường;
   - nhảy cóc → in `GAP expected=… got=… missing=…` ngay lập tức;
   - lặp → in `DUP`, bỏ khung.
3. `PING`/`PONG` theo chu kỳ `ping_ms` (1 giây khi audit bật) mang `ack`: nếu `ack` trong `PONG` nhỏ hơn `seq` của `PING` thì in `UNACKED` (bắt khung mất ở cuối luồng); không có `PONG` thì in `NO_PONG`.
4. Mỗi 200 ms đọc bộ đếm TCP của kernel (`TCP_INFO`): segment truyền lại tăng thì in `TCP_RETRANS` kèm RTT, cwnd.
5. Khi kết nối đóng: in `CONN_CLOSE` với `tx_seq`, `rx_seq`, `peer_ack`, khoảng `seq` nghi mất, số khung còn nằm trong hàng đợi (`LOST_ON_CLOSE`); với client còn in `REQ_ABORTED` cho từng request chưa có trả lời và `REQ_TIMEOUT` cho request quá 5 giây.
6. Hàng đợi gửi của server đạt 50 % giới hạn 1 MB thì in `QUEUE_HIGH`; vượt thì `QUEUE_FULL` rồi đóng kết nối.
7. Mỗi dòng ghi vào file và (khi bật) in ra terminal; dòng bất thường kết thúc bằng `!!`. Lõi client cũng đẩy dòng lên giao diện (thẻ "Mạng").
8. Mức ghi `NETCHAT_AUDIT=off | events | frames` (`frames` ghi từng khung gửi/nhận).
9. Mô phỏng mất gói để trình diễn: `FAULT` (bỏ ngẫu nhiên một phần trăm khung gửi ra) ở server hoặc lõi, bật/tắt lúc chạy; mất gói thật của mạng dùng `tc netem` trong WSL.
10. `tools/audit/merge.py` trộn log hai phía theo thời gian, ghép khung gửi–nhận theo `(conn_id của server, seq)` và in các khung gửi mà không có khung nhận tương ứng.

Định dạng dòng, danh mục sự kiện và ví dụ đầy đủ nằm ở protocol-v2.md, mục 9.

### 3.20 Bot, bộ tạo tải và đo hiệu năng

1. **Bot Python:** chạy N tài khoản tự động (`bot001`...), mỗi bot `HELLO` → `REGISTER` → `LOGIN` → kịch bản. Kịch bản: `echo`, `ramp`, `frag`, `badframe`, `enc`, `loss`, `slow`, `churn`, `chat`, `group`, `offline`.
2. **Demo cục bộ:** vài cửa sổ Electron thật (`--profile=an`, `--profile=binh`) do người điều khiển, trong khi hàng chục bot chạy nền nhắn vào cùng hệ thống; panel dev hiện thống kê và audit theo thời gian thực.
3. **Bộ tạo tải C:** một thread `epoll` giữ hàng nghìn kết nối (mục tiêu 5.000, thử 10.000), in RTT và thông lượng.
4. **Đo:** byte trên dây ÷ nội dung, số khung mỗi `recv`/`send`, số segment mỗi tin, p50/p95/p99, CPU và RSS theo số kết nối; chạy với và không có mã hóa, "chỉ mạng" và "có DB", có và không có `netem`.
5. Message dev `ECHO`, `STATS`, `FAULT` chỉ có khi server chạy với `NETCHAT_DEV=1`.

---

## 4. Yêu cầu phi chức năng

| Mã | Yêu cầu |
|---|---|
| NF-1 | Server C dùng `epoll` không chặn, một thread xử lý được nhiều kết nối đồng thời (mục tiêu 5.000). |
| NF-2 | Xử lý đúng TCP là luồng byte ở **cả hai đầu C**: nhiều khung trong một `recv`, một khung chia nhiều `recv`, header bị cắt, `send` gửi thiếu. |
| NF-3 | Tuân thủ các giới hạn ở protocol-v2.md, mục 7 (payload, tốc độ gửi, hàng đợi gửi...); vượt hàng đợi thì đóng kết nối. |
| NF-4 | Dọn tài nguyên đúng khi `recv` trả 0 hoặc lỗi: không rò fd/bộ nhớ; bạn bè và người đang chat được báo. |
| NF-5 | Ghi DB bằng transaction ngắn; lỗi DB trả `ERR_INTERNAL`, không làm sập server. |
| NF-6 | Phía mạng của client do code C quản lý; renderer không có quyền ra mạng. |
| NF-7 | Mọi dữ liệu trên dây đi qua lớp mã hóa đã thương lượng; mật khẩu không bao giờ được lưu thô. |
| NF-8 | Audit log không làm chậm đường dữ liệu đáng kể khi ở mức `events`, và tắt hẳn được (`off`) khi đo hiệu năng. |

## 5. Dữ liệu cần lưu

Thiết kế bảng cụ thể nằm ở [schema.sql](schema.sql). Các thực thể:

- **Người dùng:** username, tên hiển thị, băm mật khẩu, salt.
- **Phiên:** token, user, hạn sử dụng.
- **Quan hệ bạn bè:** hai user, trạng thái (chờ / đã chấp nhận).
- **Nhóm và thành viên:** tên, owner, thời điểm vào nhóm, trạng thái lời mời.
- **Tin nhắn:** người gửi, đích (user hoặc nhóm), nội dung, thời gian, `msg_id` tăng dần.
- **Trạng thái giao:** tin nào đã giao cho người nhận nào.

Nhật ký của hệ thống là audit log: **file** (`logs/audit-*.log`) ở mỗi bên, không nằm trong DB.

## 6. Phụ thuộc giữa các chức năng

```
Khung + HELLO + mã hóa + audit ──> Đăng ký ──> Đăng nhập ──> Danh sách online
                                                          ├─> Bạn bè (mời → chấp nhận) ──> Chat 1-1
                                                          │                              └─> Mời vào nhóm ──> Chat nhóm
                                                          └─> Tin offline (tự đẩy sau đăng nhập)
Bot, bộ tạo tải, đo: dùng từng chức năng khi nó sẵn sàng
```

Thứ tự xây dựng: khung + `PING` → mã hóa + `HELLO` → audit → tài khoản/đăng nhập → bạn bè → chat 1-1 → offline → nhóm → giao diện hoàn thiện (giao diện dựng song song từ bước đăng nhập). Từng bước kèm kịch bản bot tương ứng (protocol-v2.md, mục 14).

## 7. Kiểm thử chấp nhận

| Tình huống | Kết quả mong đợi |
|---|---|
| Gửi 2 khung liền trong một lần `send` | Server xử lý cả hai, đúng thứ tự. |
| Gửi 1 khung cắt thành nhiều mảnh, kể cả giữa header | Server ghép đúng, xử lý một lần. |
| `length` = 0 hoặc > 65536 | `ERR_PROTOCOL`, kết nối bị đóng, audit ghi `FRAME_ERR`. |
| Lệnh cần đăng nhập khi chưa xác thực | `ERR_NOT_AUTHENTICATED`. |
| `HELLO` rồi gửi khung chưa mã hóa | Server không parse được, trả `ERR_BAD_REQUEST`; kết nối không lệch pha. |
| Quan sát bằng `tcpdump` | Payload không đọc được thành chữ; header (`length`, `seq`, `ack`) đọc được. |
| Bật `FAULT` 10 % ở server | Server in `FAULT_DROP`, client in `GAP` hoặc `UNACKED` trong vài trăm micro giây; `merge.py` liệt kê đúng các khung mất. |
| Khung cuối bị bỏ (không có khung sau) | `UNACKED` xuất hiện khi `PONG` về, không quá `ping_ms` + RTT. |
| `tc netem` mất 1 % | Tin vẫn đủ và đúng thứ tự, `gaps = 0`, audit in `TCP_RETRANS`, p99 tăng. |
| Rút mạng khi đang gửi | Hai bên in `CONN_CLOSE` với khoảng `seq` nghi mất; client in `REQ_ABORTED`; sau đó `RESUME` thành công. |
| Đăng nhập cùng tài khoản từ hai cửa sổ | Cửa sổ cũ nhận `EVT_KICKED`. |
| Tắt ứng dụng khi đang chat 1-1 | Bên kia nhận `EVT_PEER_DISCONNECTED`, bạn bè nhận `EVT_USER_OFFLINE`. |
| Reload renderer trong 60 giây | Không đứt TCP; giao diện lấy lại phiên từ main. |
| Nhắn cho bạn offline, bạn đó đăng nhập | Nhận `EVT_OFFLINE_MSGS`, sau `OFFLINE_ACK` không nhận lại. |
| Rớt mạng trước khi `OFFLINE_ACK` | Lần đăng nhập sau nhận lại tin. |
| Nhắn cho người không phải bạn | `ERR_NOT_FRIENDS`. |
| Không phải owner gọi `GROUP_KICK` | `ERR_PERMISSION`. |
| Gửi > 20 message/giây | `ERR_RATE_LIMIT`. |
| Client không đọc, hàng đợi vượt 1 MB | Audit in `QUEUE_HIGH`, `QUEUE_FULL`, `LOST_ON_CLOSE`; server đóng kết nối đó, các kết nối khác không bị ảnh hưởng. |
| 5.000 kết nối đồng thời | Server vẫn phản hồi, không rò fd, không lỗi khung. |
