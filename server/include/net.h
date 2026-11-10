/*
 * net.h - tầng mạng: socket, epoll, framing (protocol.md mục 2, 4.4, 7, 9)
 *
 * Trách nhiệm:
 *   - listen/accept, epoll non-blocking, đọc/ghi từng phần
 *   - ghép/tách khung 12 byte [length][seq][ack][payload] (common/frame.h),
 *     điền seq/ack, kiểm tra seq nhận, mã hóa/giải mã payload (common/cipher.h)
 *   - hàng đợi gửi mỗi kết nối, timeout, dọn kết nối
 *
 * KHÔNG làm: parse JSON, biết "type", biết user/DB. Những thứ đó thuộc msg.h
 * và các handler. net.h chỉ giao payload (chuỗi byte UTF-8) cho tầng trên qua
 * callback và nhận payload để gửi qua net_send().
 *
 * Luồng: một thread, một vòng epoll. Mọi callback chạy trong thread đó,
 * nên không cần lock trong handler.
 */
#ifndef NET_H
#define NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>   /* ssize_t */

#include "cipher.h"
#include "frame.h"

/* ---- Hằng số lấy từ protocol-v2.md mục 7 ---- */
#define NET_MAX_PAYLOAD     FRAME_MAX_PAYLOAD   /* length hợp lệ: 1..65536 */
#define NET_HEADER_LEN      FRAME_HEADER_LEN    /* 12 byte: length, seq, ack */
#define NET_SENDQ_MAX       (1024u * 1024u) /* vượt thì đóng kết nối */
#ifndef NET_AUTH_TIMEOUT_S
#define NET_AUTH_TIMEOUT_S  30              /* chưa xác thực (tính từ lúc kết nối) */
#endif
#ifndef NET_IDLE_TIMEOUT_S
#define NET_IDLE_TIMEOUT_S  60              /* không nhận byte nào */
#endif

typedef struct net_server net_server_t;
typedef struct net_conn   net_conn_t;       /* opaque; chỉ dùng qua API dưới */

/* ================================================================
 * 1. Callback tầng trên cài vào
 * ================================================================ */

typedef struct {
    /* Kết nối mới đã accept. Thường: cấp session CONNECTED. */
    void (*on_connect)(net_conn_t *c, void *ud);

    /*
     * Nhận đủ 1 payload (NUL-terminated, hợp lệ tới khi hàm trả về;
     * muốn giữ thì copy). Thường: msg_parse + dispatch.
     */
    void (*on_payload)(net_conn_t *c, const char *payload, uint32_t len,
                       void *ud);

    /*
     * Kết nối sắp bị giải phóng (client đóng, lỗi, timeout, net_close).
     * Sau callback này con trỏ c KHÔNG còn hợp lệ.
     * Thường: đẩy EVT_USER_OFFLINE / EVT_PEER_DISCONNECTED, giữ phiên 60s.
     */
    void (*on_close)(net_conn_t *c, void *ud);

    /*
     * Tuỳ chọn, gọi ~mỗi giây: dọn phiên hết hạn, v.v. Có thể NULL.
     * Timeout 30s/60s do net tự xử lý và hỏi tầng trên qua is_authed.
     */
    void (*on_tick)(void *ud);

    /* Cho net biết kết nối đã xác thực chưa (để chọn 30s hay 60s). */
    int  (*is_authed)(net_conn_t *c, void *ud);

    void *userdata;
} net_callbacks_t;

/* ================================================================
 * 2. Vòng đời server
 * ================================================================ */

/*
 * port = 0 nghĩa là cổng mặc định 9000. on_payload bắt buộc phải khác NULL,
 * các callback khác có thể NULL. Trả NULL nếu lỗi (errno/log đã ghi).
 */
net_server_t *net_server_create(uint16_t port, const net_callbacks_t *cb);

/* Chạy vòng epoll; chặn tới khi net_server_stop(). Trả 0 nếu thoát sạch. */
int  net_server_run(net_server_t *s);

/* An toàn gọi từ signal handler (chỉ đặt cờ + đánh thức epoll). */
void net_server_stop(net_server_t *s);

/* Đóng mọi kết nối (gọi on_close từng cái) và giải phóng. */
void net_server_destroy(net_server_t *s);

/* ================================================================
 * 3. Thao tác trên kết nối
 * ================================================================ */

/* Định danh số tăng dần, dùng cho log: "conn_id=17". */
uint64_t    net_conn_id(const net_conn_t *c);
const char *net_conn_peer(const net_conn_t *c);   /* "ip:port" */

/* Con trỏ tuỳ ý tầng trên gắn vào kết nối (session). net không free. */
void  net_conn_set_data(net_conn_t *c, void *p);
void *net_conn_get_data(const net_conn_t *c);

/*
 * Mã hóa đường truyền (HELLO, protocol-v2 mục 4.2). Sau khi gọi, mọi khung gửi đi
 * từ NÀY được mã hóa và mọi khung nhận về từ NÀY được giải mã; các khung đã xếp
 * hàng đợi trước đó giữ nguyên. Gọi ngay sau khi đã net_send HELLO_RES (luôn ở
 * dạng rõ). Gọi lần nào cũng đánh dấu HELLO đã xong.
 */
void net_conn_set_cipher(net_conn_t *c, bool on);
bool net_conn_hello_done(const net_conn_t *c);

/*
 * Đếm 1 message nhận được trong giây hiện tại (cửa sổ cố định 1 giây).
 * Trả 1 nếu còn trong hạn mức max_per_sec, 0 nếu vượt (-> ERR_RATE_LIMIT).
 */
int net_conn_rate_check(net_conn_t *c, unsigned max_per_sec);

/*
 * Log ra stderr: "2026-10-27 23:10:11 conn=17 nội dung". c = NULL thì in
 * "conn=-". Dùng chung cho mọi tầng để log có cùng định dạng.
 */
void net_log(const net_conn_t *c, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/*
 * Đưa 1 payload vào hàng đợi gửi (net tự thêm header 12 byte với seq/ack rồi copy).
 * Không gọi send() trực tiếp; vòng epoll gửi dần và bật/tắt EPOLLOUT.
 * Trả:
 *    0  ok
 *   -1  len ngoài 1..NET_MAX_PAYLOAD hoặc hết bộ nhớ
 *   -2  hàng đợi vượt NET_SENDQ_MAX -> net đã đánh dấu đóng kết nối
 * Payload được copy nên caller free ngay được.
 */
int net_send(net_conn_t *c, const char *payload, uint32_t len);

/*
 * Đóng kết nối SAU KHI gửi hết hàng đợi (dùng cho LOGOUT_RES, EVT_KICKED,
 * ERR_PROTOCOL: gửi xong mới đóng). on_close sẽ được gọi ở cuối vòng lặp
 * hiện tại, không gọi đệ quy trong lúc đang ở callback.
 */
void net_close_after_flush(net_conn_t *c);

/* Đóng ngay, bỏ hàng đợi chưa gửi. */
void net_close_now(net_conn_t *c);

#endif /* NET_H */
