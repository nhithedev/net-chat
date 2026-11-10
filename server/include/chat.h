/*
 * chat.h - handler nghiệp vụ: danh sách online, bạn bè, chat 1-1
 *          (protocol.md mục 5.2, 5.3, 5.4).
 *
 * Gọi chat_init() một lần lúc khởi động (sau auth_init). auth.c gọi
 * chat_on_login / chat_on_disconnect để báo bạn bè khi trạng thái online đổi.
 */
#ifndef CHAT_H
#define CHAT_H

#include "store.h"

void chat_init(void);

/* Vừa đăng nhập thành công: EVT_USER_ONLINE cho bạn bè đang online. */
void chat_on_login(store_user_t *u);

/*
 * Kết nối đã xác thực của u vừa đóng: EVT_USER_OFFLINE cho bạn bè đang online,
 * EVT_PEER_DISCONNECTED cho ai đang mở chat 1-1 với u.
 */
void chat_on_disconnect(store_user_t *u);

#endif /* CHAT_H */
