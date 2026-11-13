/*
 * auth.c - REGISTER, LOGIN, RESUME, LOGOUT.
 */
#include "auth.h"

#include <ctype.h>
#include <string.h>

#include "chat.h"
#include "msg.h"
#include "store.h"

static store_user_t *conn_user(net_conn_t *c)
{
    return net_conn_get_data(c);
}

static int is_authed(net_conn_t *c)
{
    store_user_t *u = conn_user(c);
    return u && u->conn == c;
}

/* username: 3..20 ký tự [a-z0-9_] sau khi đổi sang chữ thường. */
bool auth_normalize_username(const char *in, char *out)
{
    size_t n = strlen(in);
    if (n < 3 || n > 20)
        return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)tolower((unsigned char)in[i]);
        if (!(ch >= 'a' && ch <= 'z') && !(ch >= '0' && ch <= '9') && ch != '_')
            return false;
        out[i] = (char)ch;
    }
    out[n] = '\0';
    return true;
}

static void handle_register(net_conn_t *c, const msg_request_t *req)
{
    if (is_authed(c)) {
        msg_reply_error(c, req, ERR_BAD_REQUEST, "Đang đăng nhập, không thể đăng ký");
        return;
    }

    const char *username, *password, *display_name;
    char name[21];
    if (!msg_get_str(req, "username", &username) ||
        !msg_get_str(req, "password", &password) ||
        !msg_get_str(req, "display_name", &display_name) ||
        !auth_normalize_username(username, name) ||
        !msg_str_len_between(password, 6, 64) ||
        !msg_str_len_between(display_name, 1, 30)) {
        msg_reply_error(c, req, ERR_BAD_REQUEST, NULL);
        return;
    }

    switch (store_user_create(name, display_name, password)) {
    case STORE_OK:
        net_log(c, "đăng ký user=%s", name);
        msg_reply_ok(c, req, NULL);
        break;
    case STORE_EXISTS:
        net_log(c, "đăng ký thất bại: user=%s đã tồn tại", name);
        msg_reply_error(c, req, ERR_ALREADY_EXISTS, "Tên đăng nhập đã được dùng");
        break;
    default:
        net_log(c, "đăng ký thất bại: lỗi lưu trữ user=%s", name);
        msg_reply_error(c, req, ERR_INTERNAL, NULL);
        break;
    }
}

/*
 * Gắn kết nối c vào u. Đăng nhập trùng (protocol 4.3): đá kết nối cũ. Gán
 * u->conn = c trước để kết nối cũ mất quyền ngay; on_close của nó sẽ không đụng
 * tới u->conn. Trả kết nối cũ (hoặc NULL).
 */
static net_conn_t *attach_session(net_conn_t *c, store_user_t *u)
{
    net_conn_t *old = u->conn;
    u->conn = c;
    net_conn_set_data(c, u);
    if (old && old != c) {
        net_log(old, "bị đá: user=%s đăng nhập ở nơi khác", u->username);
        json_builder_t *d = jb_object();
        if (d)
            jb_put_str(d, "reason", "login_elsewhere");
        msg_push_event(old, "EVT_KICKED", d);
        net_close_after_flush(old);
    }
    return old;
}

/* Sau LOGIN_RES / RESUME_RES: tin offline rồi báo bạn bè. */
static void after_authenticated(net_conn_t *c, store_user_t *u, net_conn_t *old)
{
    /* Chưa có tin offline thật: đẩy batch rỗng đúng như protocol 8.1. */
    json_builder_t *m = jb_object();
    if (m) {
        jb_put_arr(m, "messages", jb_array());
        msg_push_event(c, "EVT_OFFLINE_MSGS", m);
    }

    /* Đăng nhập đá kết nối cũ thì user vẫn online, không báo bạn bè lần nữa. */
    if (!old)
        chat_on_login(u);
}

static void handle_login(net_conn_t *c, const msg_request_t *req)
{
    if (is_authed(c)) {
        msg_reply_error(c, req, ERR_BAD_REQUEST, "Đã đăng nhập");
        return;
    }

    const char *username, *password;
    char name[21];
    if (!msg_get_str(req, "username", &username) ||
        !msg_get_str(req, "password", &password) ||
        !auth_normalize_username(username, name)) {
        msg_reply_error(c, req, ERR_BAD_REQUEST, NULL);
        return;
    }

    store_user_t *u = store_user_find(name);
    if (!u) {
        net_log(c, "đăng nhập thất bại: không có user=%s", name);
        msg_reply_error(c, req, ERR_USER_NOT_FOUND, NULL);
        return;
    }
    if (!store_user_check_password(u, password)) {
        net_log(c, "đăng nhập thất bại: sai mật khẩu user=%s", name);
        msg_reply_error(c, req, ERR_WRONG_PASSWORD, NULL);
        return;
    }

    char token[STORE_TOKEN_HEX_LEN + 1];
    if (!store_token_create(u, token)) {
        msg_reply_error(c, req, ERR_INTERNAL, NULL);
        return;
    }

    net_conn_t *old = attach_session(c, u);
    net_log(c, "đăng nhập user=%s", u->username);
    json_builder_t *d = jb_object();
    if (d) {
        jb_put_str(d, "token", token);
        jb_put_str(d, "username", u->username);
        jb_put_str(d, "display_name", u->display_name);
    }
    msg_reply_ok(c, req, d);
    after_authenticated(c, u, old);
}

static bool is_token_hex(const char *s)
{
    if (strlen(s) != STORE_TOKEN_HEX_LEN)
        return false;
    for (const char *p = s; *p; p++)
        if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f')))
            return false;
    return true;
}

/* RESUME (protocol 4.2): vào lại bằng token trong 60 giây, không cần mật khẩu. */
static void handle_resume(net_conn_t *c, const msg_request_t *req)
{
    if (is_authed(c)) {
        msg_reply_error(c, req, ERR_BAD_REQUEST, "Đã đăng nhập");
        return;
    }

    const char *token;
    if (!msg_get_str(req, "token", &token)) {
        msg_reply_error(c, req, ERR_BAD_REQUEST, NULL);
        return;
    }
    store_user_t *u = is_token_hex(token) ? store_token_resume(token) : NULL;
    if (!u) {
        net_log(c, "RESUME thất bại: token sai hoặc hết hạn");
        msg_reply_error(c, req, ERR_TOKEN_INVALID, NULL);
        return;
    }

    net_conn_t *old = attach_session(c, u);
    net_log(c, "RESUME user=%s", u->username);
    json_builder_t *d = jb_object();
    if (d) {
        jb_put_str(d, "username", u->username);
        jb_put_str(d, "display_name", u->display_name);
    }
    msg_reply_ok(c, req, d);
    after_authenticated(c, u, old);
}

/* LOGOUT (chuc-nang 3.3): hủy token, trả LOGOUT_RES rồi đóng. on_close báo bạn bè offline. */
static void handle_logout(net_conn_t *c, const msg_request_t *req)
{
    store_user_t *u = conn_user(c);        /* đã qua kiểm tra xác thực nên khác NULL */
    store_token_revoke(u);
    net_log(c, "đăng xuất user=%s", u->username);
    msg_reply_ok(c, req, NULL);
    net_close_after_flush(c);
}

int auth_net_is_authed(net_conn_t *c, void *ud)
{
    (void)ud;
    return is_authed(c);
}

void auth_net_on_close(net_conn_t *c, void *ud)
{
    (void)ud;
    store_user_t *u = conn_user(c);
    if (u && u->conn == c) {
        u->conn = NULL;
        net_log(c, "ngắt kết nối user=%s, giữ phiên %ds", u->username,
                STORE_RESUME_HOLD_S);
        store_token_hold(u);
        chat_on_disconnect(u);
    }
}

void auth_init(void)
{
    msg_register_handler(MSG_REGISTER, handle_register);
    msg_register_handler(MSG_LOGIN, handle_login);
    msg_register_handler(MSG_RESUME, handle_resume);
    msg_register_handler(MSG_LOGOUT, handle_logout);
    msg_set_auth_fn(is_authed);
}
