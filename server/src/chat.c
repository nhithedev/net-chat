/*
 * chat.c - ONLINE_LIST, bạn bè, chat 1-1 và các event đi kèm.
 * Mọi hàm chạy trong thread epoll duy nhất (xem net.h), không cần khóa.
 */
#include "chat.h"

#include <stdio.h>
#include <string.h>

#include "auth.h"
#include "msg.h"

#define HISTORY_LIMIT   50
#define TEXT_MAX_BYTES  4000

/* ---------------------------------------------------------------- tiện ích */

/* Người dùng đã đăng nhập của kết nối (handler chỉ chạy sau khi qua kiểm tra auth). */
static store_user_t *me_of(net_conn_t *c)
{
    return net_conn_get_data(c);
}

/* Gửi event cho user nếu họ đang online. */
static void push_to(const store_user_t *u, const char *evt, json_builder_t *d)
{
    if (u && u->conn)
        msg_push_event(u->conn, evt, d);
    else
        jb_free(d);
}

/* Đọc trường username trong data, chuẩn hóa chữ thường. */
static bool get_user(const msg_request_t *req, const char *key, char *out)
{
    const char *raw;
    return msg_get_str(req, key, &raw) && auth_normalize_username(raw, out);
}

/* ---------------------------------------------------------------- ONLINE_LIST */

static void online_cb(const char *username, const char *display_name, void *ud)
{
    json_builder_t *o = jb_object();
    if (!o)
        return;
    jb_put_str(o, "username", username);
    jb_put_str(o, "display_name", display_name);
    jb_add_obj(ud, o);
}

static void handle_online_list(net_conn_t *c, const msg_request_t *req)
{
    json_builder_t *d = jb_object(), *arr = jb_array();
    if (!d || !arr) {
        jb_free(d);
        jb_free(arr);
        msg_reply_error(c, req, ERR_INTERNAL, NULL);
        return;
    }
    store_online_each(online_cb, arr);
    jb_put_arr(d, "users", arr);
    msg_reply_ok(c, req, d);
}

/* ---------------------------------------------------------------- presence */

struct notify_ctx {
    store_user_t *me;
    bool          login;       /* true: vừa online; false: vừa offline */
};

static void notify_friend_cb(const char *username, const char *display_name, void *ud)
{
    (void)display_name;
    struct notify_ctx *x = ud;
    store_user_t *f = store_user_find(username);
    if (!f || !f->conn)
        return;

    json_builder_t *d = jb_object();
    if (d) {
        jb_put_str(d, "username", x->me->username);
        msg_push_event(f->conn, x->login ? "EVT_USER_ONLINE" : "EVT_USER_OFFLINE", d);
    }
    /* Bạn đang mở chat 1-1 với người vừa rớt: báo riêng (protocol 4.5). */
    if (!x->login && strcmp(f->chat_with, x->me->username) == 0) {
        json_builder_t *p = jb_object();
        if (p) {
            jb_put_str(p, "username", x->me->username);
            msg_push_event(f->conn, "EVT_PEER_DISCONNECTED", p);
        }
        f->chat_with[0] = '\0';
    }
}

void chat_on_login(store_user_t *u)
{
    struct notify_ctx x = { .me = u, .login = true };
    store_friends_each(u->username, notify_friend_cb, &x);
}

void chat_on_disconnect(store_user_t *u)
{
    struct notify_ctx x = { .me = u, .login = false };
    store_friends_each(u->username, notify_friend_cb, &x);
    u->chat_with[0] = '\0';
}

/* ---------------------------------------------------------------- bạn bè */

static void handle_friend_request(net_conn_t *c, const msg_request_t *req)
{
    store_user_t *me = me_of(c);
    char to[21];
    if (!get_user(req, "to", to) || strcmp(to, me->username) == 0) {
        msg_reply_error(c, req, ERR_BAD_REQUEST, NULL);
        return;
    }

    switch (store_friend_request(me->username, to)) {
    case STORE_OK: {
        net_log(c, "kết bạn: %s mời %s", me->username, to);
        msg_reply_ok(c, req, NULL);
        json_builder_t *d = jb_object();
        if (d) {
            jb_put_str(d, "from", me->username);
            jb_put_str(d, "display_name", me->display_name);
        }
        push_to(store_user_find(to), "EVT_FRIEND_REQUEST", d);
        break;
    }
    case STORE_EXISTS:
        msg_reply_error(c, req, ERR_ALREADY_EXISTS, "Đã là bạn bè hoặc đã gửi lời mời");
        break;
    case STORE_NOT_FOUND:
        msg_reply_error(c, req, ERR_NOT_FOUND, "Không có người dùng này");
        break;
    default:
        msg_reply_error(c, req, ERR_INTERNAL, NULL);
        break;
    }
}

/* ACCEPT / REJECT dùng chung: khác nhau ở hàm store và event gửi lại. */
static void respond_request(net_conn_t *c, const msg_request_t *req, bool accept)
{
    store_user_t *me = me_of(c);
    char from[21];
    if (!get_user(req, "from", from)) {
        msg_reply_error(c, req, ERR_BAD_REQUEST, NULL);
        return;
    }

    store_status_t st = accept ? store_friend_accept(me->username, from)
                               : store_friend_reject(me->username, from);
    if (st == STORE_NOT_FOUND) {
        msg_reply_error(c, req, ERR_NOT_FOUND, "Không có lời mời từ người này");
        return;
    }
    if (st != STORE_OK) {
        msg_reply_error(c, req, ERR_INTERNAL, NULL);
        return;
    }

    net_log(c, "kết bạn: %s %s lời mời của %s", me->username,
            accept ? "chấp nhận" : "từ chối", from);
    msg_reply_ok(c, req, NULL);

    json_builder_t *d = jb_object();
    if (d) {
        jb_put_str(d, "username", me->username);
        if (accept)
            jb_put_str(d, "display_name", me->display_name);
    }
    push_to(store_user_find(from), accept ? "EVT_FRIEND_ACCEPTED" : "EVT_FRIEND_REJECTED", d);
}

static void handle_friend_accept(net_conn_t *c, const msg_request_t *req)
{
    respond_request(c, req, true);
}

static void handle_friend_reject(net_conn_t *c, const msg_request_t *req)
{
    respond_request(c, req, false);
}

static void handle_friend_remove(net_conn_t *c, const msg_request_t *req)
{
    store_user_t *me = me_of(c);
    char other[21];
    if (!get_user(req, "username", other)) {
        msg_reply_error(c, req, ERR_BAD_REQUEST, NULL);
        return;
    }

    store_status_t st = store_friend_remove(me->username, other);
    if (st == STORE_NOT_FOUND) {
        msg_reply_error(c, req, ERR_NOT_FOUND, "Hai người không phải bạn bè");
        return;
    }
    if (st != STORE_OK) {
        msg_reply_error(c, req, ERR_INTERNAL, NULL);
        return;
    }

    net_log(c, "hủy kết bạn: %s - %s", me->username, other);
    msg_reply_ok(c, req, NULL);

    json_builder_t *d = jb_object();
    if (d)
        jb_put_str(d, "username", me->username);
    push_to(store_user_find(other), "EVT_FRIEND_REMOVED", d);
}

static void friend_cb(const char *username, const char *display_name, void *ud)
{
    json_builder_t *o = jb_object();
    if (!o)
        return;
    store_user_t *f = store_user_find(username);
    jb_put_str(o, "username", username);
    jb_put_str(o, "display_name", display_name);
    jb_put_bool(o, "online", f && f->conn);
    jb_add_obj(ud, o);
}

static void name_cb(const char *username, const char *display_name, void *ud)
{
    (void)display_name;
    jb_add_str(ud, username);
}

static void handle_friend_list(net_conn_t *c, const msg_request_t *req)
{
    store_user_t *me = me_of(c);
    json_builder_t *d = jb_object(), *fr = jb_array(), *in = jb_array(), *out = jb_array();
    if (!d || !fr || !in || !out) {
        jb_free(d);
        jb_free(fr);
        jb_free(in);
        jb_free(out);
        msg_reply_error(c, req, ERR_INTERNAL, NULL);
        return;
    }
    store_friends_each(me->username, friend_cb, fr);
    store_pending_in_each(me->username, name_cb, in);
    store_pending_out_each(me->username, name_cb, out);
    jb_put_arr(d, "friends", fr);
    jb_put_arr(d, "pending_in", in);
    jb_put_arr(d, "pending_out", out);
    msg_reply_ok(c, req, d);
}

/* ---------------------------------------------------------------- chat 1-1 */

static void history_cb(long long msg_id, const char *from, const char *text,
                       long long ts_ms, void *ud)
{
    json_builder_t *o = jb_object();
    if (!o)
        return;
    jb_put_int(o, "msg_id", msg_id);
    jb_put_str(o, "from", from);
    jb_put_str(o, "text", text);
    jb_put_int(o, "ts", ts_ms);
    jb_add_obj(ud, o);
}

static void handle_msg_send(net_conn_t *c, const msg_request_t *req)
{
    store_user_t *me = me_of(c);
    char to[21];
    const char *text;
    if (!get_user(req, "to", to) || !msg_get_str(req, "text", &text) ||
        strcmp(to, me->username) == 0 || !msg_str_len_between(text, 1, TEXT_MAX_BYTES)) {
        msg_reply_error(c, req, ERR_BAD_REQUEST, NULL);
        return;
    }
    if (strlen(text) > TEXT_MAX_BYTES) {           /* giới hạn tính theo byte */
        msg_reply_error(c, req, ERR_TOO_LARGE, NULL);
        return;
    }

    store_user_t *peer = store_user_find(to);
    if (!peer) {
        msg_reply_error(c, req, ERR_NOT_FOUND, "Không có người dùng này");
        return;
    }
    if (!store_are_friends(me->username, to)) {
        msg_reply_error(c, req, ERR_NOT_FRIENDS, NULL);
        return;
    }

    bool delivered = peer->conn != NULL;
    long long msg_id, ts;
    if (!store_msg_save(me->username, to, text, delivered, &msg_id, &ts)) {
        msg_reply_error(c, req, ERR_INTERNAL, NULL);
        return;
    }
    net_log(c, "tin nhắn %s -> %s msg_id=%lld %s", me->username, to, msg_id,
            delivered ? "(đã giao)" : "(lưu offline)");   /* không ghi nội dung */

    json_builder_t *d = jb_object();
    if (d) {
        jb_put_int(d, "msg_id", msg_id);
        jb_put_int(d, "ts", ts);
        jb_put_bool(d, "delivered", delivered);
    }
    msg_reply_ok(c, req, d);

    if (delivered) {
        json_builder_t *e = jb_object();
        if (e) {
            jb_put_int(e, "msg_id", msg_id);
            jb_put_str(e, "from", me->username);
            jb_put_str(e, "text", text);
            jb_put_int(e, "ts", ts);
        }
        push_to(peer, "EVT_MSG", e);
    }
}

static void handle_chat_open(net_conn_t *c, const msg_request_t *req)
{
    store_user_t *me = me_of(c);
    char with[21];
    if (!get_user(req, "with", with) || strcmp(with, me->username) == 0) {
        msg_reply_error(c, req, ERR_BAD_REQUEST, NULL);
        return;
    }
    if (!store_user_find(with)) {
        msg_reply_error(c, req, ERR_NOT_FOUND, "Không có người dùng này");
        return;
    }
    if (!store_are_friends(me->username, with)) {
        msg_reply_error(c, req, ERR_NOT_FRIENDS, NULL);
        return;
    }

    json_builder_t *d = jb_object(), *arr = jb_array();
    if (!d || !arr) {
        jb_free(d);
        jb_free(arr);
        msg_reply_error(c, req, ERR_INTERNAL, NULL);
        return;
    }
    /* Chuyển sang người khác chỉ là mở lại với người mới (protocol 5.4). */
    snprintf(me->chat_with, sizeof me->chat_with, "%s", with);
    store_msg_history(me->username, with, HISTORY_LIMIT, history_cb, arr);
    jb_put_arr(d, "history", arr);
    msg_reply_ok(c, req, d);
}

static void handle_chat_close(net_conn_t *c, const msg_request_t *req)
{
    store_user_t *me = me_of(c);
    char with[21];
    if (!get_user(req, "with", with)) {
        msg_reply_error(c, req, ERR_BAD_REQUEST, NULL);
        return;
    }

    if (strcmp(me->chat_with, with) == 0)
        me->chat_with[0] = '\0';
    msg_reply_ok(c, req, NULL);

    /* Bên còn lại chỉ cần biết nếu họ đang mở chat với mình. */
    store_user_t *peer = store_user_find(with);
    if (peer && peer->conn && strcmp(peer->chat_with, me->username) == 0) {
        json_builder_t *d = jb_object();
        if (d)
            jb_put_str(d, "username", me->username);
        push_to(peer, "EVT_CHAT_CLOSED", d);
    }
}

void chat_init(void)
{
    msg_register_handler(MSG_ONLINE_LIST,    handle_online_list);
    msg_register_handler(MSG_FRIEND_REQUEST, handle_friend_request);
    msg_register_handler(MSG_FRIEND_ACCEPT,  handle_friend_accept);
    msg_register_handler(MSG_FRIEND_REJECT,  handle_friend_reject);
    msg_register_handler(MSG_FRIEND_REMOVE,  handle_friend_remove);
    msg_register_handler(MSG_FRIEND_LIST,    handle_friend_list);
    msg_register_handler(MSG_MSG_SEND,       handle_msg_send);
    msg_register_handler(MSG_CHAT_OPEN,      handle_chat_open);
    msg_register_handler(MSG_CHAT_CLOSE,     handle_chat_close);
}
