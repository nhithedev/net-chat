# Đặc tả chức năng hệ thống Chat

Tài liệu này mô tả **hệ thống làm gì**. Giao thức chi tiết (khung dữ liệu, message, mã lỗi) nằm ở [protocol.md](protocol.md); tài liệu này tham chiếu tới nó thay vì lặp lại. Khi có chênh lệch về số liệu hoặc tên message, **protocol.md là bản chuẩn**.

## 1. Mục tiêu và phạm vi

Ứng dụng chat nhiều người dùng cho bài tập lớn Lập trình mạng: đăng ký/đăng nhập, bạn bè, chat 1-1, chat nhóm, tin nhắn offline, ghi log hoạt động, giao diện đồ họa.

Trọng tâm chấm điểm là **lập trình mạng**: xử lý luồng TCP, cơ chế I/O của server, quản lý phiên, xử lý ngắt kết nối.

**Ngoài phạm vi (v1):** gửi file/ảnh, mã hóa đầu cuối, TLS (nâng cấp sau), thông báo đẩy, gọi thoại/video.

## 2. Kiến trúc

```
React (trình duyệt) --WebSocket--> Gateway (Node.js) --TCP--> Server C (epoll) --> PostgreSQL
```

| Thành phần | Trách nhiệm |
|---|---|
| Client React | Giao diện, giữ trạng thái hiển thị, sinh `id` request, tự gửi `PING`, tự khử trùng tin theo `msg_id`. |
| Gateway Node.js | Cầu nối: mỗi WebSocket ↔ đúng một kết nối TCP; thêm/bỏ tiền tố độ dài 4 byte; **không parse JSON**, không chứa logic nghiệp vụ. |
| Server C | Toàn bộ nghiệp vụ: framing, epoll, parse JSON, xác thực, phiên, presence, định tuyến tin, rate limit, ghi DB, log. |
| PostgreSQL | Lưu user, bạn bè, nhóm, tin nhắn, trạng thái giao, log. |

## 3. Tác nhân

- **Khách (chưa đăng nhập):** chỉ đăng ký, đăng nhập, khôi phục phiên (và `PING`).
- **Người dùng:** dùng mọi chức năng sau khi xác thực.
- **Owner nhóm:** người dùng có thêm quyền đuổi thành viên.

## 4. Yêu cầu chức năng

Mức ưu tiên: **P0** = bắt buộc theo đề; **P1** = nên có; **P2** = cộng điểm.

### 4.1 Tài khoản (P0)

| Mã | Yêu cầu | Message |
|---|---|---|
| F-ACC-1 | Đăng ký: username 3–20 ký tự `a-z 0-9 _`, mật khẩu 6–64, tên hiển thị 1–30. Username trùng bị từ chối. | `REGISTER` |
| F-ACC-2 | Mật khẩu lưu dạng băm SHA-256 + salt riêng từng user, không lưu bản rõ. | — |
| F-ACC-3 | Đổi mật khẩu (cần mật khẩu cũ). | `CHANGE_PASSWORD` |
| F-ACC-4 | Đổi tên hiển thị. | `UPDATE_PROFILE` |

### 4.2 Đăng nhập và phiên (P0)

| Mã | Yêu cầu | Message |
|---|---|---|
| F-SES-1 | Đăng nhập trả `token` ngẫu nhiên 32 byte hex. | `LOGIN` |
| F-SES-2 | Kết nối chưa xác thực quá 30 giây bị đóng. | — |
| F-SES-3 | Mất kết nối đột ngột: giữ phiên 60 giây, client dùng `token` để vào lại không cần mật khẩu (F5 trang không bắt đăng nhập lại). | `RESUME` |
| F-SES-4 | Đăng xuất hủy token và đóng kết nối. | `LOGOUT` |
| F-SES-5 | Mỗi tài khoản một kết nối: đăng nhập nơi thứ hai thì kết nối cũ nhận `EVT_KICKED` (`login_elsewhere`) rồi bị đóng. | `EVT_KICKED` |
| F-SES-6 | Heartbeat: client gửi `PING` mỗi 20 giây khi rảnh; server đóng nếu 60 giây không nhận byte nào. | `PING`/`PONG` |

### 4.3 Hiện diện / danh sách online (P0)

| Mã | Yêu cầu | Message |
|---|---|---|
| F-PRE-1 | Sau khi đăng nhập xem được danh sách mọi user đang online. | `ONLINE_LIST` |
| F-PRE-2 | Bạn bè nhận thông báo realtime khi một người đăng nhập hoặc ngắt kết nối. | `EVT_USER_ONLINE`, `EVT_USER_OFFLINE` |
| F-PRE-3 | Online/offline tính theo kết nối đang mở, không tính thời gian giữ phiên. | — |

### 4.4 Bạn bè (P0)

| Mã | Yêu cầu | Message |
|---|---|---|
| F-FRD-1 | Gửi lời mời kết bạn; không tự mời chính mình; không mời người đã là bạn hoặc đã mời. | `FRIEND_REQUEST` |
| F-FRD-2 | Người nhận chấp nhận hoặc từ chối; người mời được báo kết quả. | `FRIEND_ACCEPT`, `FRIEND_REJECT` |
| F-FRD-3 | Hủy kết bạn; bên kia được báo. | `FRIEND_REMOVE` |
| F-FRD-4 | Xem danh sách bạn kèm trạng thái online, lời mời đến, lời mời đã gửi. | `FRIEND_LIST` |
| F-FRD-5 | Lời mời gửi lúc người kia offline vẫn được lưu và hiện khi họ đăng nhập. | — |

### 4.5 Chat 1-1 (P0)

| Mã | Yêu cầu | Message |
|---|---|---|
| F-DM-1 | Chỉ nhắn được cho bạn bè (`ERR_NOT_FRIENDS` nếu không). Tối đa 4000 byte/tin. | `MSG_SEND` |
| F-DM-2 | Người nhận online nhận tin ngay; người gửi biết tin đã giao (`delivered`) hay lưu offline. | `EVT_MSG` |
| F-DM-3 | Mở cuộc trò chuyện tải 50 tin gần nhất; chuyển sang người khác bằng cách mở lại với người mới. | `CHAT_OPEN` |
| F-DM-4 | Dừng cuộc trò chuyện; bên còn lại được báo. | `CHAT_CLOSE`, `EVT_CHAT_CLOSED` |
| F-DM-5 | Khi một bên rớt mạng/ngắt kết nối, bên còn lại được báo. | `EVT_PEER_DISCONNECTED` |

### 4.6 Chat nhóm (P0)

| Mã | Yêu cầu | Message |
|---|---|---|
| F-GRP-1 | Tạo nhóm (tên 1–40 ký tự), người tạo là owner. | `GROUP_CREATE` |
| F-GRP-2 | Thành viên mời bạn bè của mình vào nhóm; người được mời chấp nhận hoặc từ chối. | `GROUP_INVITE`, `GROUP_ACCEPT`, `GROUP_DECLINE` |
| F-GRP-3 | Chỉ owner được đuổi thành viên. | `GROUP_KICK` |
| F-GRP-4 | Thành viên rời nhóm. Owner rời thì quyền chuyển cho người vào sớm nhất; nhóm hết người thì xóa. | `GROUP_LEAVE` |
| F-GRP-5 | Xem danh sách nhóm đã tham gia, lời mời nhóm, thành viên (kèm online). | `GROUP_LIST`, `GROUP_MEMBERS` |
| F-GRP-6 | Gửi tin nhóm; mọi thành viên trừ người gửi nhận realtime. Xem 50 tin gần nhất. Tối đa 100 thành viên/nhóm. | `GROUP_MSG_SEND`, `EVT_GROUP_MSG`, `GROUP_HISTORY` |
| F-GRP-7 | Thành viên được báo khi có người vào, rời, bị đuổi. | `EVT_GROUP_JOINED`, `EVT_GROUP_LEFT`, `EVT_GROUP_KICKED` |

### 4.7 Tin nhắn offline (P0)

| Mã | Yêu cầu | Message |
|---|---|---|
| F-OFF-1 | Tin gửi cho người không có kết nối (1-1 hoặc nhóm) được lưu với trạng thái chưa giao. | — |
| F-OFF-2 | Ngay sau `LOGIN`/`RESUME` thành công, server đẩy tin chưa giao theo batch (kích thước theo protocol), sắp theo `msg_id` tăng dần. | `EVT_OFFLINE_MSGS` |
| F-OFF-3 | Client xác nhận bằng `OFFLINE_ACK`; server đánh dấu đã giao và đẩy batch kế. Chưa ACK thì gửi lại lần sau (giao ít nhất một lần, client khử trùng theo `msg_id`). | `OFFLINE_ACK` |

### 4.8 Ghi log hoạt động (P0)

| Mã | Yêu cầu |
|---|---|
| F-LOG-1 | Server ghi log các sự kiện: kết nối, đăng ký, đăng nhập (thành công/thất bại), đăng xuất, ngắt kết nối, timeout, lỗi giao thức, kết bạn, thay đổi nhóm, gửi tin (không ghi nội dung). |
| F-LOG-2 | Mỗi dòng log có thời gian, `conn_id`, username (nếu đã xác thực), loại sự kiện. |
| F-LOG-3 | Log ra file và/hoặc bảng `activity_logs` trong DB. |

### 4.9 Giao diện đồ họa (P0, 3 điểm)

| Mã | Yêu cầu |
|---|---|
| F-UI-1 | Màn hình đăng nhập / đăng ký, hiển thị lỗi theo `error.code`. |
| F-UI-2 | Bố cục chính: thanh bên (bạn bè, nhóm, lời mời) + khung chat. |
| F-UI-3 | Danh sách bạn có chấm online/offline, cập nhật realtime. |
| F-UI-4 | Khung chat: lịch sử, ô nhập, tin mới tự cuộn, phân biệt tin của mình / người khác. |
| F-UI-5 | Chuyển hội thoại không mất lịch sử đã tải. |
| F-UI-6 | Hiển thị trạng thái kết nối (đang kết nối, mất kết nối, đang khôi phục); tự `RESUME` khi rớt mạng và khi tải lại trang (lưu token ở `sessionStorage`). |
| F-UI-7 | Thông báo: lời mời bạn bè, lời mời nhóm, bị kick, bị đuổi khỏi nhóm, bạn bè rớt mạng. |
| F-UI-8 | Đánh dấu tin chưa giao / đã giao theo `delivered`. |

### 4.10 Tính năng mở rộng (P2, 2–5 điểm, chọn một vài)

Gợi ý theo độ khả thi: chỉ báo "đang gõ"; badge số tin chưa đọc; emoji; tìm kiếm tin trong hội thoại; TLS (`wss://` + TLS ở Server C); sửa/xóa tin nhắn.

## 5. Yêu cầu phi chức năng

| Mã | Yêu cầu |
|---|---|
| NF-1 | Server C dùng `epoll` không chặn, một luồng xử lý được nhiều kết nối đồng thời. |
| NF-2 | Xử lý đúng TCP là luồng byte: nhiều message trong một `recv`, một message chia nhiều `recv`, header bị cắt. |
| NF-3 | Tuân thủ các giới hạn ở mục 7 của [protocol.md](protocol.md) (kích thước payload, tốc độ gửi, hàng đợi gửi...); vượt hàng đợi thì đóng kết nối. |
| NF-4 | Dọn tài nguyên đúng khi `recv` trả 0 hoặc lỗi: không rò fd/bộ nhớ, bạn bè và người đang chat được báo. |
| NF-5 | Ghi DB bằng transaction ngắn; lỗi DB trả `ERR_INTERNAL`, không làm sập server. |
| NF-6 | Gateway không sửa nội dung; mất một bên thì đóng bên kia. |

## 6. Dữ liệu cần lưu (mức khái niệm)

Thiết kế bảng cụ thể nằm ở [schema.sql](schema.sql). Các thực thể cần có:

- **Người dùng:** username, tên hiển thị, băm mật khẩu, salt.
- **Phiên:** token, user, hạn sử dụng.
- **Quan hệ bạn bè:** hai user, trạng thái (chờ / đã chấp nhận).
- **Nhóm và thành viên:** tên, owner, thời điểm vào nhóm, trạng thái lời mời.
- **Tin nhắn:** người gửi, đích (user hoặc nhóm), nội dung, thời gian, `msg_id` tăng dần.
- **Trạng thái giao:** tin nào đã giao cho người nhận nào.
- **Log hoạt động.**

## 7. Luồng chính

1. Mở trang → kết nối WebSocket → có `token` lưu sẵn thì `RESUME`, không thì hiện màn đăng nhập.
2. `LOGIN` → nhận `EVT_OFFLINE_MSGS` → `OFFLINE_ACK`.
3. Tải `FRIEND_LIST`, `GROUP_LIST`, `ONLINE_LIST` để dựng giao diện.
4. Chọn hội thoại → `CHAT_OPEN`/`GROUP_HISTORY` → gửi/nhận tin.
5. Rớt mạng → giao diện báo mất kết nối → tự kết nối lại và `RESUME` trong 60 giây.

Ví dụ message đầy đủ: xem mục 8 của [protocol.md](protocol.md).

## 8. Kiểm thử chấp nhận

| Tình huống | Kết quả mong đợi |
|---|---|
| Gửi 2 message liền trong một lần `send` | Server xử lý cả hai, đúng thứ tự. |
| Gửi 1 message cắt thành nhiều mảnh, kể cả cắt giữa header | Server ghép đúng, xử lý một lần. |
| `length` = 0 hoặc > 65536 | `ERR_PROTOCOL`, kết nối bị đóng. |
| Lệnh cần đăng nhập khi chưa xác thực | `ERR_NOT_AUTHENTICATED`. |
| Đăng nhập cùng tài khoản từ hai trình duyệt | Trình duyệt cũ nhận `EVT_KICKED`. |
| Tắt tab khi đang chat 1-1 | Bên kia nhận `EVT_PEER_DISCONNECTED`, bạn bè nhận `EVT_USER_OFFLINE`. |
| F5 trong 60 giây | `RESUME` thành công, không cần mật khẩu. |
| Nhắn cho bạn offline, bạn đó đăng nhập | Nhận `EVT_OFFLINE_MSGS`, sau `OFFLINE_ACK` không nhận lại. |
| Rớt mạng trước khi `OFFLINE_ACK` | Lần đăng nhập sau nhận lại tin. |
| Nhắn cho người không phải bạn | `ERR_NOT_FRIENDS`. |
| Không phải owner gọi `GROUP_KICK` | `ERR_PERMISSION`. |
| Gửi > 20 message/giây | `ERR_RATE_LIMIT`. |
| Client không đọc, hàng đợi vượt 1 MB | Server đóng kết nối. |
| 100+ kết nối đồng thời | Server vẫn phản hồi, không rò fd. |
