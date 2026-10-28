/* Unit test cho tầng msg: parse, get_*, builder, build_* (không cần socket). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "msg.h"

static int failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            failures++;                                                    \
        }                                                                  \
    } while (0)

static msg_request_t *parse(const char *s)
{
    err_code_t e;
    return msg_parse(s, (uint32_t)strlen(s), &e);
}

static void test_type_table(void)
{
    CHECK(msg_type_from_str("LOGIN") == MSG_LOGIN);
    CHECK(msg_type_from_str("GROUP_MSG_SEND") == MSG_GROUP_MSG_SEND);
    CHECK(msg_type_from_str("login") == MSG_UNKNOWN);        /* phân biệt hoa/thường */
    CHECK(msg_type_from_str("") == MSG_UNKNOWN);
    CHECK(msg_type_from_str(NULL) == MSG_UNKNOWN);
    CHECK(strcmp(msg_type_name(MSG_FRIEND_LIST), "FRIEND_LIST") == 0);
    CHECK(!msg_type_needs_auth(MSG_LOGIN));
    CHECK(!msg_type_needs_auth(MSG_PING));
    CHECK(msg_type_needs_auth(MSG_MSG_SEND));
    CHECK(msg_type_needs_auth(MSG_OFFLINE_ACK));
    /* mọi type đều round-trip tên <-> enum */
    for (int t = MSG_UNKNOWN + 1; t < MSG_TYPE_COUNT; t++)
        CHECK(msg_type_from_str(msg_type_name((msg_type_t)t)) == (msg_type_t)t);
}

static void test_parse_valid(void)
{
    msg_request_t *r = parse(
        "{\"type\":\"LOGIN\",\"id\":\"c-2\",\"data\":"
        "{\"username\":\"an\",\"n\":42,\"flag\":true,\"extra\":[1,2]}}");
    CHECK(r != NULL);
    CHECK(msg_req_type(r) == MSG_LOGIN);
    CHECK(strcmp(msg_req_id(r), "c-2") == 0);

    const char *s; int64_t n; bool b;
    CHECK(msg_get_str(r, "username", &s) && strcmp(s, "an") == 0);
    CHECK(msg_get_int(r, "n", &n) && n == 42);
    CHECK(msg_get_bool(r, "flag", &b) && b);
    /* sai kiểu / thiếu trường */
    CHECK(!msg_get_str(r, "n", &s));
    CHECK(!msg_get_int(r, "username", &n));
    CHECK(!msg_get_bool(r, "username", &b));
    CHECK(!msg_get_str(r, "khong_co", &s));
    msg_request_free(r);
}

static void test_get_int_edge(void)
{
    msg_request_t *r = parse(
        "{\"type\":\"PING\",\"id\":\"1\",\"data\":"
        "{\"frac\":1.5,\"big\":9007199254740992,\"huge\":1e300,\"neg\":-7}}");
    int64_t n;
    CHECK(!msg_get_int(r, "frac", &n));                       /* không nguyên */
    CHECK(msg_get_int(r, "big", &n) && n == 9007199254740992LL);
    CHECK(!msg_get_int(r, "huge", &n));
    CHECK(msg_get_int(r, "neg", &n) && n == -7);
    msg_request_free(r);
}

static void test_parse_invalid(void)
{
    err_code_t e = ERR_NONE;
    CHECK(msg_parse("not json", 8, &e) == NULL && e == ERR_BAD_REQUEST);
    CHECK(msg_parse("[1,2,3]", 7, &e) == NULL && e == ERR_BAD_REQUEST);
    CHECK(msg_parse("\"str\"", 5, &e) == NULL);
    CHECK(msg_parse("{\"type\":", 8, &e) == NULL);            /* cụt */
}

static void test_parse_malformed_fields_still_return_request(void)
{
    /* thiếu id: vẫn trả request để reply lỗi; handler không bao giờ thấy nó */
    msg_request_t *r = parse("{\"type\":\"LOGIN\"}");
    CHECK(r != NULL);
    CHECK(msg_req_id(r) == NULL);
    msg_request_free(r);

    r = parse("{\"type\":\"FOO\",\"id\":\"x\"}");
    CHECK(r && msg_req_type(r) == MSG_UNKNOWN && strcmp(msg_req_id(r), "x") == 0);
    msg_request_free(r);

    r = parse("{\"type\":\"LOGIN\",\"id\":\"x\",\"data\":5}");
    CHECK(r != NULL);
    msg_request_free(r);
}

static void test_str_len(void)
{
    CHECK(msg_str_len_between("abc", 3, 3));
    CHECK(!msg_str_len_between("ab", 3, 20));
    CHECK(!msg_str_len_between("abcd", 1, 3));
    CHECK(msg_str_len_between("", 0, 5));
    /* đếm theo ký tự, không theo byte: "Bình" = 4 ký tự, 5 byte */
    CHECK(msg_str_len_between("Bình", 4, 4));
    CHECK(msg_str_len_between("Nhóm LTM", 8, 8));
    CHECK(msg_str_len_between("😀😀", 2, 2));
    /* UTF-8 sai */
    CHECK(!msg_str_len_between("\xff\xfe", 0, 10));
    CHECK(!msg_str_len_between("\xc3", 0, 10));               /* cụt */
    CHECK(!msg_str_len_between("\xc0\x80", 0, 10));           /* overlong */
    CHECK(!msg_str_len_between("\xed\xa0\x80", 0, 10));       /* surrogate */
    CHECK(!msg_str_len_between(NULL, 0, 10));
}

static char *build_ok(const char *req_json, json_builder_t *data)
{
    msg_request_t *r = parse(req_json);
    uint32_t n;
    char *s = msg_build_ok(r, data, &n);
    CHECK(s && n == strlen(s));
    msg_request_free(r);
    return s;
}

static void test_build_ok(void)
{
    char *s = build_ok("{\"type\":\"REGISTER\",\"id\":\"c-1\"}", NULL);
    CHECK(strcmp(s, "{\"type\":\"REGISTER_RES\",\"id\":\"c-1\",\"ok\":true}") == 0);
    free(s);

    json_builder_t *d = jb_object();
    jb_put_str(d, "token", "9f2c");
    jb_put_int(d, "msg_id", 42);
    jb_put_bool(d, "delivered", true);
    s = build_ok("{\"type\":\"LOGIN\",\"id\":\"c-2\"}", d);
    CHECK(strcmp(s, "{\"type\":\"LOGIN_RES\",\"id\":\"c-2\",\"ok\":true,\"data\":"
                    "{\"token\":\"9f2c\",\"msg_id\":42,\"delivered\":true}}") == 0);
    free(s);
}

static void test_build_ok_big_numbers_and_arrays(void)
{
    json_builder_t *d = jb_object();
    jb_put_int(d, "ts", 1761412345678LL);
    json_builder_t *users = jb_array();
    json_builder_t *u = jb_object();
    jb_put_str(u, "username", "an");
    jb_add_obj(users, u);
    jb_add_str(users, "binh");
    jb_put_arr(d, "users", users);
    char *s = build_ok("{\"type\":\"ONLINE_LIST\",\"id\":\"c-9\"}", d);
    CHECK(strstr(s, "\"ts\":1761412345678") != NULL);
    CHECK(strstr(s, "\"users\":[{\"username\":\"an\"},\"binh\"]") != NULL);
    free(s);
}

static void test_build_error(void)
{
    msg_request_t *r = parse("{\"type\":\"LOGIN\",\"id\":\"c-17\"}");
    uint32_t n;
    char *s = msg_build_error(r, ERR_WRONG_PASSWORD, "Sai mật khẩu", &n);
    CHECK(strcmp(s, "{\"type\":\"LOGIN_RES\",\"id\":\"c-17\",\"ok\":false,"
                    "\"error\":{\"code\":1002,\"message\":\"Sai mật khẩu\"}}") == 0);
    free(s);

    /* message NULL -> mặc định theo mã lỗi */
    s = msg_build_error(r, ERR_NOT_FRIENDS, NULL, &n);
    CHECK(strstr(s, "\"code\":4") != NULL);
    CHECK(strstr(s, msg_err_default_message(ERR_NOT_FRIENDS)) != NULL);
    free(s);
    msg_request_free(r);

    /* request có type lạ -> ERROR_RES, vẫn giữ id */
    r = parse("{\"type\":\"FOO\",\"id\":\"z\"}");
    s = msg_build_error(r, ERR_BAD_REQUEST, NULL, &n);
    CHECK(strstr(s, "\"type\":\"ERROR_RES\"") != NULL);
    CHECK(strstr(s, "\"id\":\"z\"") != NULL);
    free(s);
    msg_request_free(r);

    s = msg_build_error_raw(NULL, ERR_PROTOCOL, "x", &n);
    CHECK(strstr(s, "\"id\"") == NULL);
    CHECK(strstr(s, "\"code\":10") != NULL);
    free(s);
}

static void test_build_event_and_pong(void)
{
    uint32_t n;
    json_builder_t *d = jb_object();
    jb_put_str(d, "from", "an");
    jb_put_str(d, "text", "chào \"Bình\"\nxuống dòng \\ ok");
    char *s = msg_build_event("EVT_MSG", d, &n);
    CHECK(strncmp(s, "{\"type\":\"EVT_MSG\",\"data\":{", 26) == 0);
    CHECK(strstr(s, "\"id\"") == NULL && strstr(s, "\"ok\"") == NULL);
    /* escape đúng và giữ nguyên tiếng Việt: parse lại ra đúng chuỗi cũ */
    msg_request_t *back = NULL;
    {
        /* dựng lại 1 request giả để dùng msg_get_str trên "data" */
        char tmp[512];
        snprintf(tmp, sizeof tmp, "{\"type\":\"PING\",\"id\":\"1\",%s", s + 1 + strlen("\"type\":\"EVT_MSG\","));
        back = parse(tmp);
    }
    const char *text;
    CHECK(back && msg_get_str(back, "text", &text));
    CHECK(strcmp(text, "chào \"Bình\"\nxuống dòng \\ ok") == 0);
    msg_request_free(back);
    free(s);

    CHECK(msg_build_event("MSG", NULL, &n) == NULL);          /* thiếu tiền tố EVT_ */

    msg_request_t *p = parse("{\"type\":\"PING\",\"id\":\"c-5\"}");
    s = msg_build_pong(p, &n);
    CHECK(strcmp(s, "{\"type\":\"PONG\",\"id\":\"c-5\"}") == 0);
    free(s);
    msg_request_free(p);
    s = msg_build_pong(NULL, &n);
    CHECK(strcmp(s, "{\"type\":\"PONG\"}") == 0);
    free(s);
}

int main(void)
{
    struct { const char *name; void (*fn)(void); } tests[] = {
        { "bảng type",                         test_type_table },
        { "parse request hợp lệ + get_*",      test_parse_valid },
        { "get_int: biên",                     test_get_int_edge },
        { "parse: JSON hỏng / không phải object", test_parse_invalid },
        { "parse: thiếu id, type lạ, data sai",  test_parse_malformed_fields_still_return_request },
        { "độ dài chuỗi UTF-8",                test_str_len },
        { "build_ok",                          test_build_ok },
        { "build_ok: số lớn, mảng",            test_build_ok_big_numbers_and_arrays },
        { "build_error",                       test_build_error },
        { "build_event, pong, escape",         test_build_event_and_pong },
    };
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; i++) {
        int before = failures;
        tests[i].fn();
        printf("[%s] %s\n", failures == before ? " ok " : "FAIL", tests[i].name);
    }
    printf("test_msg: %s\n", failures ? "CÓ LỖI" : "tất cả đạt");
    return failures ? 1 : 0;
}
