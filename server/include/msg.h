/*
 * msg.h - tầng message: JSON <-> struct, dựng response/event, mã lỗi
 *         (protocol.md mục 3, 5, 6)
 *
 * Trách nhiệm:
 *   - parse payload thành msg_t, kiểm tra cấu trúc chung (type, id)
 *   - bảng tra type -> handler, kiểm tra trạng thái xác thực
 *   - dựng response / error / event thành chuỗi JSON để đưa cho net_send
 *
 * KHÔNG làm: socket, epoll, framing. Hàm build_* trả chuỗi JSON thô; ghép
 * thêm header 4 byte là việc của net_send().
 *
 * Thư viện JSON: cJSON (một file .c/.h, dễ vendoring). Mọi hàm get_* dưới
 * đây bọc cJSON để handler không phụ thuộc trực tiếp vào nó.
 */
#ifndef MSG_H
#define MSG_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "net.h"

/* ================================================================
 * 1. Mã lỗi (protocol.md mục 6)
 * ================================================================ */

typedef enum {
    ERR_NONE              = 0,
    ERR_BAD_REQUEST       = 1,
    ERR_NOT_AUTHENTICATED = 2,
    ERR_PERMISSION        = 3,
    ERR_NOT_FRIENDS       = 4,
    ERR_NOT_MEMBER        = 5,
    ERR_ALREADY_EXISTS    = 6,
    ERR_NOT_FOUND         = 7,
    ERR_TOO_LARGE         = 8,
    ERR_RATE_LIMIT        = 9,
    ERR_PROTOCOL          = 10,
    ERR_INTERNAL          = 11,
    /* nhóm tài khoản 1000+ */
    ERR_USER_NOT_FOUND    = 1001,
    ERR_WRONG_PASSWORD    = 1002,
    ERR_TOKEN_INVALID     = 1003
} err_code_t;

/* ================================================================
 * 2. Danh sách type request
 *    Dùng X-macro để enum, chuỗi tên và cờ "cần đăng nhập" luôn khớp nhau.
 *    X(TÊN, cần_đăng_nhập)
 * ================================================================ */

#define MSG_TYPES(X)                       \
    X(REGISTER,        0)                  \
    X(LOGIN,           0)                  \
    X(RESUME,          0)                  \
    X(PING,            0)                  \
    X(LOGOUT,          1)                  \
    X(CHANGE_PASSWORD, 1)                  \
    X(UPDATE_PROFILE,  1)                  \
    X(ONLINE_LIST,     1)                  \
    X(FRIEND_REQUEST,  1)                  \
    X(FRIEND_ACCEPT,   1)                  \
    X(FRIEND_REJECT,   1)                  \
    X(FRIEND_REMOVE,   1)                  \
    X(FRIEND_LIST,     1)                  \
    X(MSG_SEND,        1)                  \
    X(CHAT_OPEN,       1)                  \
    X(CHAT_CLOSE,      1)                  \
    X(GROUP_CREATE,    1)                  \
    X(GROUP_INVITE,    1)                  \
    X(GROUP_ACCEPT,    1)                  \
    X(GROUP_DECLINE,   1)                  \
    X(GROUP_KICK,      1)                  \
    X(GROUP_LEAVE,     1)                  \
    X(GROUP_LIST,      1)                  \
    X(GROUP_MEMBERS,   1)                  \
    X(GROUP_MSG_SEND,  1)                  \
    X(GROUP_HISTORY,   1)                  \
    X(OFFLINE_ACK,     1)

typedef enum {
    MSG_UNKNOWN = 0,
#define X(name, auth) MSG_##name,
    MSG_TYPES(X)
#undef X
    MSG_TYPE_COUNT
} msg_type_t;

/* "LOGIN" -> MSG_LOGIN; không biết thì MSG_UNKNOWN. */
msg_type_t  msg_type_from_str(const char *s);
const char *msg_type_name(msg_type_t t);            /* MSG_LOGIN -> "LOGIN" */
bool        msg_type_needs_auth(msg_type_t t);

/* ================================================================
 * 3. Request đã parse
 * ================================================================ */

typedef struct msg_request msg_request_t;           /* opaque, giữ cây JSON */

/*
 * Parse 1 payload. Trả NULL (và *err_out = ERR_BAD_REQUEST) chỉ khi payload
 * không phải một JSON object hợp lệ. Nếu là object thì luôn trả về request,
 * kể cả khi thiếu "type"/"id" hay "type" lạ (msg_req_type = MSG_UNKNOWN),
 * để còn trả lỗi kèm đúng id. msg_dispatch tự kiểm tra các trường hợp đó;
 * handler luôn nhận request đã hợp lệ: "type" biết, "id" là chuỗi không
 * rỗng (riêng PING có thể không có id: msg_req_id trả NULL), "data" (nếu
 * có) là object.
 */
msg_request_t *msg_parse(const char *payload, uint32_t len,
                         err_code_t *err_out);
void           msg_request_free(msg_request_t *r);

msg_type_t  msg_req_type(const msg_request_t *r);
const char *msg_req_id(const msg_request_t *r);

/*
 * Lấy tham số từ "data". Trả true nếu có và đúng kiểu; ngược lại false và
 * handler trả ERR_BAD_REQUEST. Chuỗi trả về do request giữ, không free,
 * hết hạn khi msg_request_free. Trường lạ bị bỏ qua (mục 3.4).
 */
bool msg_get_str (const msg_request_t *r, const char *key, const char **out);
bool msg_get_int (const msg_request_t *r, const char *key, int64_t *out);
bool msg_get_bool(const msg_request_t *r, const char *key, bool *out);

/* Kiểm tra độ dài UTF-8 theo byte và theo ký tự, dùng cho validate field. */
bool msg_str_len_between(const char *s, size_t min_chars, size_t max_chars);

/* ================================================================
 * 4. Dựng JSON gửi đi
 *    "data" được dựng bằng builder để handler không nối chuỗi tay
 *    (tránh lỗi escape: dấu ", \, xuống dòng, tiếng Việt).
 * ================================================================ */

typedef struct json_builder json_builder_t;         /* opaque */

json_builder_t *jb_object(void);                    /* {} */
json_builder_t *jb_array(void);                     /* [] */
void jb_put_str (json_builder_t *o, const char *key, const char *v);
void jb_put_int (json_builder_t *o, const char *key, int64_t v);
void jb_put_bool(json_builder_t *o, const char *key, bool v);
void jb_put_obj (json_builder_t *o, const char *key, json_builder_t *child);
void jb_put_arr (json_builder_t *o, const char *key, json_builder_t *child);
void jb_add_obj (json_builder_t *arr, json_builder_t *child);
void jb_add_str (json_builder_t *arr, const char *v);
void jb_free(json_builder_t *j);                    /* chỉ khi chưa gắn vào cha */

/*
 * Các hàm build_* trả chuỗi malloc'd (caller free), *out_len là độ dài
 * (có thể truyền NULL nếu không cần; chuỗi luôn NUL-terminated).
 * `data` được SỞ HỮU bởi hàm (đã gắn vào kết quả); truyền NULL nếu không có.
 * Trả NULL nếu hết bộ nhớ.
 */

/* {"type":"<req>_RES","id":..,"ok":true[,"data":..]} */
char *msg_build_ok(const msg_request_t *req, json_builder_t *data,
                   uint32_t *out_len);

/* {"type":"<req>_RES","id":..,"ok":false,"error":{"code":..,"message":..}} */
char *msg_build_error(const msg_request_t *req, err_code_t code,
                      const char *message, uint32_t *out_len);

/* Lỗi khi không có request hợp lệ (JSON hỏng): type "ERROR_RES", id nếu có. */
char *msg_build_error_raw(const char *id, err_code_t code,
                          const char *message, uint32_t *out_len);

/* {"type":"EVT_...","data":..}  - type_name phải bắt đầu bằng "EVT_". */
char *msg_build_event(const char *evt_type, json_builder_t *data,
                      uint32_t *out_len);

/* {"type":"PONG"[,"id":..]} */
char *msg_build_pong(const msg_request_t *ping, uint32_t *out_len);

/* Tên mặc định cho mã lỗi, vd ERR_NOT_FRIENDS -> "ERR_NOT_FRIENDS". */
const char *msg_err_name(err_code_t code);
const char *msg_err_default_message(err_code_t code);

/* ================================================================
 * 5. Gửi tiện lợi: build + net_send + free trong một lệnh
 *    Trả 0 nếu ok, âm nếu lỗi (cùng quy ước net_send).
 * ================================================================ */

int msg_reply_ok   (net_conn_t *c, const msg_request_t *req,
                    json_builder_t *data);
int msg_reply_error(net_conn_t *c, const msg_request_t *req,
                    err_code_t code, const char *message /* NULL = mặc định */);
int msg_push_event (net_conn_t *c, const char *evt_type, json_builder_t *data);

/* ================================================================
 * 6. Dispatch
 * ================================================================ */

/*
 * Handler của một lệnh. Tự validate data bằng msg_get_*, tự trả lời bằng
 * msg_reply_ok / msg_reply_error (đúng một lần). `session` là con trỏ
 * net_conn_get_data(c) (NULL nếu chưa đăng nhập).
 */
typedef void (*msg_handler_t)(net_conn_t *c, const msg_request_t *req);

/* Đăng ký handler cho từng type; gọi lúc khởi động. */
void msg_register_handler(msg_type_t t, msg_handler_t h);

/*
 * Cho msg biết kết nối đã xác thực chưa (do tầng session cài vào).
 * Chưa cài (NULL) thì mọi kết nối coi như chưa đăng nhập.
 */
typedef int (*msg_auth_fn)(net_conn_t *c);
void msg_set_auth_fn(msg_auth_fn f);

/*
 * Điểm vào duy nhất từ net: dán thẳng vào net_callbacks_t.on_payload.
 *   1. msg_parse                    -> không phải JSON object: ERR_BAD_REQUEST
 *   2. rate limit 20 msg/s          -> ERR_RATE_LIMIT
 *   3. "PONG" từ client             -> bỏ qua (không trả lời)
 *   4. thiếu type/id, sai kiểu data -> ERR_BAD_REQUEST (PING được thiếu id)
 *      type lạ                      -> ERR_BAD_REQUEST
 *   5. needs_auth && chưa đăng nhập -> ERR_NOT_AUTHENTICATED
 *   6. PING chưa có handler riêng   -> PONG (xử lý sẵn)
 *   7. gọi handler đã đăng ký       -> chưa đăng ký: ERR_INTERNAL
 * Request chỉ sống trong lúc handler chạy; muốn giữ thì copy dữ liệu ra.
 */
void msg_dispatch(net_conn_t *c, const char *payload, uint32_t len,
                  void *userdata);

#endif /* MSG_H */
