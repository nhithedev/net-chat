/*
 * net.h - tầng mạng: socket, epoll, framing (protocol.md mục 2, 4.4, 7, 9)
 *
 * Trách nhiệm:
 *   - listen/accept, epoll non-blocking, đọc/ghi từng phần
 *   - ghép/tách frame [uint32 length BE][payload]
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

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>   /* ssize_t */

/* ---- Hằng số lấy từ protocol.md mục 7 ---- */
#define NET_MAX_PAYLOAD     65536u          /* length hợp lệ: 1..65536 */
#define NET_HEADER_LEN      4u
#define NET_SENDQ_MAX       (1024u * 1024u) /* vượt thì đóng kết nối */
#define NET_AUTH_TIMEOUT_S  30              /* chưa xác thực */
#define NET_IDLE_TIMEOUT_S  60              /* không nhận byte nào */

typedef struct net_server net_server_t;
typedef struct net_conn   net_conn_t;       /* opaque; chỉ dùng qua API dưới */

/* ================================================================
 * 1. Framing thuần (không socket) - để unit test được riêng
 * ================================================================ */

typedef enum {
    FRAME_NEED_MORE = 0,   /* chưa đủ byte, chờ recv tiếp */
    FRAME_OK        = 1,   /* đã lấy được 1 payload */
    FRAME_ERROR     = -1   /* length == 0 hoặc > NET_MAX_PAYLOAD */
} frame_status_t;

/* Bộ đệm nhận của một kết nối. Chịu được header bị cắt, nhiều frame/recv. */
typedef struct {
    uint8_t *buf;
    size_t   len;          /* số byte đang có */
    size_t   cap;         
} frame_decoder_t;

void frame_decoder_init(frame_decoder_t *d);
void frame_decoder_free(frame_decoder_t *d);

/* Thêm byte vừa recv được. Trả 0 nếu ok, -1 nếu hết bộ nhớ. */
int  frame_decoder_feed(frame_decoder_t *d, const void *data, size_t n);

/*
 * Thử lấy 1 payload. Gọi lặp tới khi != FRAME_OK.
 * FRAME_OK: *payload trỏ vào bên trong buffer của decoder, *len là độ dài;
 *           con trỏ chỉ hợp lệ tới lần gọi feed/next kế tiếp (không free).
 *           Payload được NUL-terminate sẵn để parse JSON trực tiếp.
 */
frame_status_t frame_decoder_next(frame_decoder_t *d,
                                  const char **payload, uint32_t *len);

/*
 * Ghi header + payload vào out (cần NET_HEADER_LEN + len byte).
 * Trả tổng số byte ghi, hoặc -1 nếu len ngoài 1..NET_MAX_PAYLOAD.
 */
ssize_t frame_encode(uint8_t *out, size_t out_cap,
                     const char *payload, uint32_t len);

/* ================================================================
 * 2. Callback tầng trên cài vào
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
 * 3. Vòng đời server
 * ================================================================ */

/* port mặc định 9000. Trả NULL nếu lỗi (errno/log đã ghi). */
net_server_t *net_server_create(uint16_t port, const net_callbacks_t *cb);

/* Chạy vòng epoll; chặn tới khi net_server_stop(). Trả 0 nếu thoát sạch. */
int  net_server_run(net_server_t *s);

/* An toàn gọi từ signal handler (chỉ đặt cờ + đánh thức epoll). */
void net_server_stop(net_server_t *s);

/* Đóng mọi kết nối (gọi on_close từng cái) và giải phóng. */
void net_server_destroy(net_server_t *s);

/* ================================================================
 * 4. Thao tác trên kết nối
 * ================================================================ */

/* Định danh số tăng dần, dùng cho log: "conn_id=17". */
uint64_t    net_conn_id(const net_conn_t *c);
const char *net_conn_peer(const net_conn_t *c);   /* "ip:port" */

/* Con trỏ tuỳ ý tầng trên gắn vào kết nối (session). net không free. */
void  net_conn_set_data(net_conn_t *c, void *p);
void *net_conn_get_data(const net_conn_t *c);

/*
 * Đưa 1 payload vào hàng đợi gửi (net tự thêm header 4 byte rồi copy).
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
