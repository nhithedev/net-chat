/*
 * auth.h - handler REGISTER / LOGIN và trạng thái đăng nhập của kết nối
 *          (protocol.md 4.1, 4.3, 5.1).
 *
 * Dữ liệu gắn vào kết nối (net_conn_set_data) là store_user_t* của người đã
 * đăng nhập, NULL nếu chưa. Kết nối được coi là đã xác thực khi
 * user->conn == c, nên khi đăng nhập nơi khác, kết nối cũ mất quyền ngay.
 */
#ifndef AUTH_H
#define AUTH_H

#include <stdbool.h>

#include "net.h"

/* username hợp lệ (3..20 ký tự a-z 0-9 _, không phân biệt hoa thường) -> chữ thường.
 * out cần ít nhất 21 byte. Trả false nếu sai định dạng. */
bool auth_normalize_username(const char *in, char *out);

/* Đăng ký handler và hàm kiểm tra xác thực vào msg. Gọi một lần lúc khởi động. */
void auth_init(void);

/* Cài vào net_callbacks_t.is_authed và on_close. */
int  auth_net_is_authed(net_conn_t *c, void *ud);
void auth_net_on_close(net_conn_t *c, void *ud);

#endif /* AUTH_H */
