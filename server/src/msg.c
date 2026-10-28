/*
 * msg.c - tầng message: parse request, dựng response/event, dispatch. 
 * Dùng cJSON để parse/build JSON.
 */
#include "msg.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#define MSG_RATE_LIMIT_PER_SEC 20

/* ================================================================
 * 1. Mã lỗi
 * ================================================================ */

static const struct {
    err_code_t  code;
    const char *name;
    const char *message;
} ERR_TABLE[] = {
    { ERR_BAD_REQUEST,       "ERR_BAD_REQUEST",       "Yêu cầu không hợp lệ" },
    { ERR_NOT_AUTHENTICATED, "ERR_NOT_AUTHENTICATED", "Chưa đăng nhập" },
    { ERR_PERMISSION,        "ERR_PERMISSION",        "Không đủ quyền" },
    { ERR_NOT_FRIENDS,       "ERR_NOT_FRIENDS",       "Hai người chưa là bạn bè" },
    { ERR_NOT_MEMBER,        "ERR_NOT_MEMBER",        "Không phải thành viên nhóm" },
    { ERR_ALREADY_EXISTS,    "ERR_ALREADY_EXISTS",    "Đã tồn tại" },
    { ERR_NOT_FOUND,         "ERR_NOT_FOUND",         "Không tìm thấy" },
    { ERR_TOO_LARGE,         "ERR_TOO_LARGE",         "Vượt quá giới hạn kích thước" },
    { ERR_RATE_LIMIT,        "ERR_RATE_LIMIT",        "Gửi quá nhanh" },
    { ERR_PROTOCOL,          "ERR_PROTOCOL",          "Lỗi giao thức" },
    { ERR_INTERNAL,          "ERR_INTERNAL",          "Lỗi phía server" },
    { ERR_USER_NOT_FOUND,    "ERR_USER_NOT_FOUND",    "Tài khoản không tồn tại" },
    { ERR_WRONG_PASSWORD,    "ERR_WRONG_PASSWORD",    "Sai mật khẩu" },
    { ERR_TOKEN_INVALID,     "ERR_TOKEN_INVALID",     "Token không hợp lệ hoặc đã hết hạn" },
};

static const size_t ERR_TABLE_N = sizeof ERR_TABLE / sizeof ERR_TABLE[0];

const char *msg_err_name(err_code_t code)
{
    for (size_t i = 0; i < ERR_TABLE_N; i++)
        if (ERR_TABLE[i].code == code)
            return ERR_TABLE[i].name;
    return "ERR_UNKNOWN";
}

const char *msg_err_default_message(err_code_t code)
{
    for (size_t i = 0; i < ERR_TABLE_N; i++)
        if (ERR_TABLE[i].code == code)
            return ERR_TABLE[i].message;
    return "Lỗi không xác định";
}

/* ================================================================
 * 2. Bảng type (sinh từ X-macro trong msg.h)
 * ================================================================ */

static const char *const TYPE_NAMES[MSG_TYPE_COUNT] = {
    [MSG_UNKNOWN] = "",
#define X(name, auth) [MSG_##name] = #name,
    MSG_TYPES(X)
#undef X
};

static const bool TYPE_NEEDS_AUTH[MSG_TYPE_COUNT] = {
    [MSG_UNKNOWN] = false,
#define X(name, auth) [MSG_##name] = (auth),
    MSG_TYPES(X)
#undef X
};

msg_type_t msg_type_from_str(const char *s)
{
    if (!s)
        return MSG_UNKNOWN;
    for (int t = MSG_UNKNOWN + 1; t < MSG_TYPE_COUNT; t++)
        if (strcmp(s, TYPE_NAMES[t]) == 0)
            return (msg_type_t)t;
    return MSG_UNKNOWN;
}

const char *msg_type_name(msg_type_t t)
{
    return (t > MSG_UNKNOWN && t < MSG_TYPE_COUNT) ? TYPE_NAMES[t] : "";
}

bool msg_type_needs_auth(msg_type_t t)
{
    return (t > MSG_UNKNOWN && t < MSG_TYPE_COUNT) ? TYPE_NEEDS_AUTH[t] : false;
}

/* ================================================================
 * 3. Request
 * ================================================================ */

struct msg_request {
    cJSON      *root;
    cJSON      *data;          /* NULL nếu request không có "data" */
    msg_type_t  type;
    const char *type_str;      /* trỏ vào root, có thể NULL */
    const char *id;            /* trỏ vào root, NULL nếu thiếu/sai */
    bool        well_formed;   /* có type, id hợp lệ và data (nếu có) là object */
};

msg_request_t *msg_parse(const char *payload, uint32_t len, err_code_t *err_out)
{
    cJSON *root = cJSON_ParseWithLength(payload, len);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        if (err_out)
            *err_out = ERR_BAD_REQUEST;
        return NULL;
    }

    msg_request_t *r = calloc(1, sizeof *r);
    if (!r) {
        cJSON_Delete(root);
        if (err_out)
            *err_out = ERR_INTERNAL;
        return NULL;
    }
    r->root = root;
    r->well_formed = true;

    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (cJSON_IsString(type) && type->valuestring[0] != '\0') {
        r->type_str = type->valuestring;
        r->type = msg_type_from_str(r->type_str);
    } else {
        r->well_formed = false;
    }

    /* "id" bắt buộc, trừ PING (protocol 5.7: giữ nguyên id nếu PING có id). */
    cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    if (cJSON_IsString(id) && id->valuestring[0] != '\0')
        r->id = id->valuestring;
    else if (r->type != MSG_PING)
        r->well_formed = false;

    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (data) {
        if (cJSON_IsObject(data))
            r->data = data;
        else
            r->well_formed = false;
    }

    if (err_out)
        *err_out = ERR_NONE;
    return r;
}

void msg_request_free(msg_request_t *r)
{
    if (!r)
        return;
    cJSON_Delete(r->root);
    free(r);
}

msg_type_t  msg_req_type(const msg_request_t *r) { return r->type; }
const char *msg_req_id(const msg_request_t *r)   { return r->id; }

bool msg_get_str(const msg_request_t *r, const char *key, const char **out)
{
    if (!r->data)
        return false;
    cJSON *v = cJSON_GetObjectItemCaseSensitive(r->data, key);
    if (!cJSON_IsString(v) || !v->valuestring)
        return false;
    *out = v->valuestring;
    return true;
}

bool msg_get_int(const msg_request_t *r, const char *key, int64_t *out)
{
    if (!r->data)
        return false;
    cJSON *v = cJSON_GetObjectItemCaseSensitive(r->data, key);
    if (!cJSON_IsNumber(v))
        return false;
    double d = v->valuedouble;
    /* số nguyên an toàn của JavaScript: |d| <= 2^53 */
    if (d != floor(d) || fabs(d) > 9007199254740992.0)
        return false;
    *out = (int64_t)d;
    return true;
}

bool msg_get_bool(const msg_request_t *r, const char *key, bool *out)
{
    if (!r->data)
        return false;
    cJSON *v = cJSON_GetObjectItemCaseSensitive(r->data, key);
    if (!cJSON_IsBool(v))
        return false;
    *out = cJSON_IsTrue(v);
    return true;
}

/* Đếm số ký tự của chuỗi UTF-8; chuỗi sai UTF-8 thì coi là không hợp lệ. */
bool msg_str_len_between(const char *s, size_t min_chars, size_t max_chars)
{
    if (!s)
        return false;
    const unsigned char *p = (const unsigned char *)s;
    size_t chars = 0;
    while (*p) {
        size_t n;
        uint32_t cp;
        if (*p < 0x80)                { n = 1; cp = *p; }
        else if ((*p & 0xE0) == 0xC0) { n = 2; cp = *p & 0x1F; }
        else if ((*p & 0xF0) == 0xE0) { n = 3; cp = *p & 0x0F; }
        else if ((*p & 0xF8) == 0xF0) { n = 4; cp = *p & 0x07; }
        else return false;
        for (size_t i = 1; i < n; i++) {
            if ((p[i] & 0xC0) != 0x80)   /* cũng bắt luôn '\0' giữa chừng */
                return false;
            cp = (cp << 6) | (p[i] & 0x3F);
        }
        static const uint32_t MIN_CP[5] = { 0, 0, 0x80, 0x800, 0x10000 };
        if (n > 1 && cp < MIN_CP[n])                    return false; /* overlong */
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        p += n;
        chars++;
    }
    return chars >= min_chars && chars <= max_chars;
}

/* ================================================================
 * 4. Dựng JSON
 * ================================================================ */

json_builder_t *jb_object(void) { return (json_builder_t *)cJSON_CreateObject(); }
json_builder_t *jb_array(void)  { return (json_builder_t *)cJSON_CreateArray(); }

void jb_put_str(json_builder_t *o, const char *key, const char *v)
{
    cJSON_AddStringToObject((cJSON *)o, key, v ? v : "");
}
void jb_put_int(json_builder_t *o, const char *key, int64_t v)
{
    cJSON_AddNumberToObject((cJSON *)o, key, (double)v);
}
void jb_put_bool(json_builder_t *o, const char *key, bool v)
{
    cJSON_AddBoolToObject((cJSON *)o, key, v);
}
void jb_put_obj(json_builder_t *o, const char *key, json_builder_t *child)
{
    cJSON_AddItemToObject((cJSON *)o, key, (cJSON *)child);
}
void jb_put_arr(json_builder_t *o, const char *key, json_builder_t *child)
{
    cJSON_AddItemToObject((cJSON *)o, key, (cJSON *)child);
}
void jb_add_obj(json_builder_t *arr, json_builder_t *child)
{
    cJSON_AddItemToArray((cJSON *)arr, (cJSON *)child);
}
void jb_add_str(json_builder_t *arr, const char *v)
{
    cJSON_AddItemToArray((cJSON *)arr, cJSON_CreateString(v ? v : ""));
}
void jb_free(json_builder_t *j) { cJSON_Delete((cJSON *)j); }

/* In root ra chuỗi malloc'd rồi hủy root. */
static char *print_and_free(cJSON *root, uint32_t *out_len)
{
    char *s = root ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    if (s && out_len)
        *out_len = (uint32_t)strlen(s);
    return s;
}

/* Tên type của response: "<REQ>_RES", type lạ thì "ERROR_RES". */
static void res_type_name(const msg_request_t *req, char *buf, size_t cap)
{
    const char *base = (req && req->type != MSG_UNKNOWN)
                       ? msg_type_name(req->type) : "ERROR";
    snprintf(buf, cap, "%s_RES", base);
}

/* Tạo khung {"type":..., "id":...?}; nhận sở hữu `data` nếu lỗi. */
static cJSON *res_skeleton(const char *type, const char *id, json_builder_t *data)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        jb_free(data);
        return NULL;
    }
    cJSON_AddStringToObject(root, "type", type);
    if (id)
        cJSON_AddStringToObject(root, "id", id);
    return root;
}

char *msg_build_ok(const msg_request_t *req, json_builder_t *data,
                   uint32_t *out_len)
{
    char type[64];
    res_type_name(req, type, sizeof type);
    cJSON *root = res_skeleton(type, req ? req->id : NULL, data);
    if (!root)
        return NULL;
    cJSON_AddBoolToObject(root, "ok", true);
    if (data)
        cJSON_AddItemToObject(root, "data", (cJSON *)data);
    return print_and_free(root, out_len);
}

static char *build_error(const char *type, const char *id, err_code_t code,
                         const char *message, uint32_t *out_len)
{
    cJSON *root = res_skeleton(type, id, NULL);
    if (!root)
        return NULL;
    cJSON_AddBoolToObject(root, "ok", false);
    cJSON *err = cJSON_AddObjectToObject(root, "error");
    if (err) {
        cJSON_AddNumberToObject(err, "code", (double)code);
        cJSON_AddStringToObject(err, "message",
                                message ? message : msg_err_default_message(code));
    }
    return print_and_free(root, out_len);
}

char *msg_build_error(const msg_request_t *req, err_code_t code,
                      const char *message, uint32_t *out_len)
{
    char type[64];
    res_type_name(req, type, sizeof type);
    return build_error(type, req ? req->id : NULL, code, message, out_len);
}

char *msg_build_error_raw(const char *id, err_code_t code,
                          const char *message, uint32_t *out_len)
{
    return build_error("ERROR_RES", id, code, message, out_len);
}

char *msg_build_event(const char *evt_type, json_builder_t *data,
                      uint32_t *out_len)
{
    if (!evt_type || strncmp(evt_type, "EVT_", 4) != 0) {
        jb_free(data);
        return NULL;
    }
    cJSON *root = res_skeleton(evt_type, NULL, data);
    if (!root)
        return NULL;
    if (data)
        cJSON_AddItemToObject(root, "data", (cJSON *)data);
    return print_and_free(root, out_len);
}

char *msg_build_pong(const msg_request_t *ping, uint32_t *out_len)
{
    cJSON *root = res_skeleton("PONG", ping ? ping->id : NULL, NULL);
    return print_and_free(root, out_len);
}

/* ================================================================
 * 5. Gửi tiện lợi
 * ================================================================ */

/*
 * Gửi chuỗi JSON do msg_build_* trả về rồi free. Độ dài lấy bằng strlen tại
 * đây: KHÔNG truyền "&len" của build_* làm đối số cùng lúc với "len", vì thứ
 * tự tính đối số trong C không xác định (len có thể bị đọc trước khi được ghi).
 */
static int send_and_free(net_conn_t *c, char *json)
{
    if (!json)
        return -1;
    int rc = net_send(c, json, (uint32_t)strlen(json));
    free(json);
    return rc;
}

int msg_reply_ok(net_conn_t *c, const msg_request_t *req, json_builder_t *data)
{
    return send_and_free(c, msg_build_ok(req, data, NULL));
}

int msg_reply_error(net_conn_t *c, const msg_request_t *req,
                    err_code_t code, const char *message)
{
    return send_and_free(c, msg_build_error(req, code, message, NULL));
}

int msg_push_event(net_conn_t *c, const char *evt_type, json_builder_t *data)
{
    return send_and_free(c, msg_build_event(evt_type, data, NULL));
}

/* ================================================================
 * 6. Dispatch
 * ================================================================ */

static msg_handler_t g_handlers[MSG_TYPE_COUNT];
static msg_auth_fn   g_auth_fn;

void msg_register_handler(msg_type_t t, msg_handler_t h)
{
    if (t > MSG_UNKNOWN && t < MSG_TYPE_COUNT)
        g_handlers[t] = h;
}

void msg_set_auth_fn(msg_auth_fn f)
{
    g_auth_fn = f;
}

void msg_dispatch(net_conn_t *c, const char *payload, uint32_t len, void *userdata)
{
    (void)userdata;

    err_code_t err;
    msg_request_t *r = msg_parse(payload, len, &err);
    if (!r) {
        net_log(c, "payload không phải JSON object hợp lệ");
        send_and_free(c, msg_build_error_raw(NULL, ERR_BAD_REQUEST,
                                             "JSON không hợp lệ", NULL));
        return;
    }

    if (!net_conn_rate_check(c, MSG_RATE_LIMIT_PER_SEC)) {
        msg_reply_error(c, r, ERR_RATE_LIMIT, NULL);
        goto done;
    }

    /* PONG (client trả lời PING của server) có thể không có id: bỏ qua. */
    if (r->type_str && strcmp(r->type_str, "PONG") == 0)
        goto done;

    if (!r->well_formed) {
        msg_reply_error(c, r, ERR_BAD_REQUEST,
                        "Thiếu \"type\"/\"id\" hoặc \"data\" không phải object");
        goto done;
    }

    if (r->type == MSG_UNKNOWN) {
        net_log(c, "type không biết: %.40s", r->type_str);
        msg_reply_error(c, r, ERR_BAD_REQUEST, "Lệnh không được hỗ trợ");
        goto done;
    }

    if (msg_type_needs_auth(r->type) && !(g_auth_fn && g_auth_fn(c))) {
        msg_reply_error(c, r, ERR_NOT_AUTHENTICATED, NULL);
        goto done;
    }

    if (g_handlers[r->type]) {
        g_handlers[r->type](c, r);
    } else if (r->type == MSG_PING) {
        send_and_free(c, msg_build_pong(r, NULL));
    } else {
        net_log(c, "chưa có handler cho %s", msg_type_name(r->type));
        msg_reply_error(c, r, ERR_INTERNAL, "Lệnh chưa được cài đặt");
    }

done:
    msg_request_free(r);
}
