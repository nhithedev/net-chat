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

typedef struct {
    char         username[21];          /* chữ thường, 3..20 */
    char         display_name[121];     /* 1..30 ký tự UTF-8 (tối đa 120 byte) */
    char         salt[STORE_SALT_HEX_LEN + 1]; /* salt riêng cho mỗi user, hex */
    char         hash[STORE_HASH_HEX_LEN + 1];
    net_conn_t  *conn;                  /* kết nối đang đăng nhập, NULL = offline */
} store_user_t;

typedef enum {
    STORE_OK = 0,
    STORE_EXISTS,                       /* username đã dùng */
    STORE_ERROR                         /* hết bộ nhớ, lỗi sinh ngẫu nhiên... */
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
 * Kết nối PostgreSQL (conninfo của libpq, vd "host=127.0.0.1 dbname=chat
 * user=chat password=chat"). Gọi một lần trước khi nhận kết nối nào. Trả
 * false (và log lý do) nếu không kết nối được.
 */
bool store_init(const char *conninfo);

/* Đóng kết nối DB và giải phóng cache. */
void store_shutdown(void);

#endif /* STORE_H */
