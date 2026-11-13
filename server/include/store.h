/*
 * store.h - nơi lưu user và token, lưu trong PostgreSQL (bảng users/sessions
 * trong schema.sql). Handler chỉ gọi các hàm dưới đây, không biết SQL.
 * User đã tải được giữ trong cache RAM để con trỏ store_user_t* ổn định và
 * để có chỗ đặt trường `conn` (trạng thái online, không lưu vào DB).
 *
 * Một thread, không lock (xem net.h).
 */
#ifndef STORE_H
#define STORE_H

#include <stdbool.h>
#include <stddef.h>

#include "net.h"

#define STORE_SALT_HEX_LEN   32         /* 16 byte ngẫu nhiên */
#define STORE_HASH_HEX_LEN   64         /* SHA-256 */
#define STORE_TOKEN_HEX_LEN  64         /* 32 byte ngẫu nhiên (protocol 4.2) */
#define STORE_RESUME_HOLD_S  60         /* giữ phiên sau khi mất kết nối (protocol 4.2) */

typedef struct {
    long long    id;                    /* users.id trong DB */
    char         chat_with[21];         /* đang mở chat 1-1 với ai ("" = không ai) */
    char         username[21];          /* chữ thường, 3..20 */
    char         display_name[121];     /* 1..30 ký tự UTF-8 (tối đa 120 byte) */
    char         salt[STORE_SALT_HEX_LEN + 1]; /* salt riêng cho mỗi user, hex */
    char         hash[STORE_HASH_HEX_LEN + 1];
    net_conn_t  *conn;                  /* kết nối đang đăng nhập, NULL = offline */
} store_user_t;

typedef enum {
    STORE_OK = 0,
    STORE_EXISTS,                       /* username đã dùng */
    STORE_ERROR,                        /* hết bộ nhớ, lỗi sinh ngẫu nhiên... */
    STORE_NOT_FOUND                     /* không có user / lời mời / quan hệ tương ứng */
} store_status_t;

/* Tạo user; mật khẩu được băm SHA-256 + salt riêng, không lưu bản rõ. */
store_status_t store_user_create(const char *username, const char *display_name,
                                 const char *password);

/* Trả NULL nếu không có (hoặc lỗi DB). Con trỏ giữ hợp lệ tới store_shutdown. */
store_user_t *store_user_find(const char *username);

/* Mật khẩu nhập vào có đúng không? */
bool store_user_check_password(const store_user_t *u, const char *password);

/*
 * Sinh token ngẫu nhiên gắn với user để dùng cho RESUME sau này, ghi vào token_out (cần
 * STORE_TOKEN_HEX_LEN + 1 byte). Trả false nếu lỗi.
 */
bool store_token_create(const store_user_t *u, char *token_out);

/*
 * RESUME (protocol 4.2): token còn hạn thì trả user của nó và gia hạn lại (1 ngày,
 * vì kết nối mới đã thành kết nối đang mở). NULL nếu token sai, đã hết hạn hoặc lỗi DB.
 */
store_user_t *store_token_resume(const char *token);

/* LOGOUT: hủy mọi token của u (không RESUME được nữa). */
void store_token_revoke(const store_user_t *u);

/*
 * Kết nối của u vừa đóng: mọi token của u chỉ còn dùng được thêm STORE_RESUME_HOLD_S
 * giây (token hết hạn sớm hơn thì giữ nguyên). Gọi từ on_close.
 */
void store_token_hold(const store_user_t *u);

/* ---------------------------------------------------------------- bạn bè */

typedef void (*store_user_cb)(const char *username, const char *display_name,
                              void *ud);

/* STORE_OK | STORE_EXISTS (đã là bạn / đã có lời mời chờ) | STORE_NOT_FOUND (không có `to`). */
store_status_t store_friend_request(const char *from, const char *to);
/* `me` chấp nhận / từ chối lời mời của `from`. NOT_FOUND nếu không có lời mời chờ. */
store_status_t store_friend_accept(const char *me, const char *from);
store_status_t store_friend_reject(const char *me, const char *from);
/* Hủy kết bạn. NOT_FOUND nếu vốn không phải bạn. */
store_status_t store_friend_remove(const char *me, const char *other);

bool store_are_friends(const char *a, const char *b);

/* Duyệt bạn bè / lời mời đến / lời mời đã gửi của `me`, theo username tăng dần. */
void store_friends_each(const char *me, store_user_cb cb, void *ud);
void store_pending_in_each(const char *me, store_user_cb cb, void *ud);
void store_pending_out_each(const char *me, store_user_cb cb, void *ud);

/* Duyệt mọi user đang có kết nối (online). */
void store_online_each(store_user_cb cb, void *ud);

/* --------------------------------------------------------------- tin nhắn */

typedef void (*store_msg_cb)(long long msg_id, const char *from, const char *text,
                             long long ts_ms, void *ud);

/*
 * Lưu một tin 1-1 (tự tạo hội thoại nếu chưa có). `delivered` = người nhận đang
 * online và đã được đẩy tin; false thì tin được đánh dấu chưa giao.
 */
bool store_msg_save(const char *from, const char *to, const char *text,
                    bool delivered, long long *msg_id, long long *ts_ms);

/* Tối đa `limit` tin gần nhất giữa a và b, thứ tự thời gian tăng dần. */
void store_msg_history(const char *a, const char *b, int limit,
                       store_msg_cb cb, void *ud);

/*
 * Kết nối PostgreSQL (conninfo của libpq, vd "host=127.0.0.1 dbname=chat
 * user=chat password=chat"). Gọi một lần trước khi nhận kết nối nào. Trả
 * false (và log lý do) nếu không kết nối được.
 */
bool store_init(const char *conninfo);

/* Đóng kết nối DB và giải phóng cache. */
void store_shutdown(void);

#endif /* STORE_H */
