/* Unit test cho frame_decoder / frame_encode (không dùng socket). */
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net.h"

static int failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            failures++;                                                    \
        }                                                                  \
    } while (0)

static size_t make_frame(uint8_t *out, const char *payload)
{
    return (size_t)frame_encode(out, 70000, payload, (uint32_t)strlen(payload));
}

static void test_encode_matches_spec(void)
{
    /* Ví dụ trong protocol.md mục 2 */
    uint8_t out[64];
    ssize_t n = frame_encode(out, sizeof out, "{\"type\":\"PING\"}", 15);
    static const uint8_t want[] = {
        0x00, 0x00, 0x00, 0x0F, 0x7B, 0x22, 0x74, 0x79, 0x70, 0x65, 0x22,
        0x3A, 0x22, 0x50, 0x49, 0x4E, 0x47, 0x22, 0x7D };
    CHECK(n == (ssize_t)sizeof want);
    CHECK(memcmp(out, want, sizeof want) == 0);
}

static void test_encode_limits(void)
{
    uint8_t out[16];
    CHECK(frame_encode(out, sizeof out, "x", 0) == -1);
    CHECK(frame_encode(out, sizeof out, "x", NET_MAX_PAYLOAD + 1) == -1);
    CHECK(frame_encode(out, 4, "ab", 2) == -1);          /* out quá nhỏ */
    CHECK(frame_encode(out, 6, "ab", 2) == 6);
}

static void test_single_frame(void)
{
    frame_decoder_t d;
    frame_decoder_init(&d);
    uint8_t f[128];
    size_t n = make_frame(f, "{\"a\":1}");
    CHECK(frame_decoder_feed(&d, f, n) == 0);

    const char *p; uint32_t len;
    CHECK(frame_decoder_next(&d, &p, &len) == FRAME_OK);
    CHECK(len == 7);
    CHECK(strcmp(p, "{\"a\":1}") == 0);                  /* NUL-terminated */
    CHECK(frame_decoder_next(&d, &p, &len) == FRAME_NEED_MORE);
    frame_decoder_free(&d);
}

static void test_byte_by_byte(void)
{
    /* header bị cắt ở mọi vị trí */
    frame_decoder_t d;
    frame_decoder_init(&d);
    uint8_t f[128];
    size_t n = make_frame(f, "{\"type\":\"PING\"}");
    int got = 0;
    for (size_t i = 0; i < n; i++) {
        CHECK(frame_decoder_feed(&d, f + i, 1) == 0);
        const char *p; uint32_t len;
        frame_status_t st = frame_decoder_next(&d, &p, &len);
        if (i + 1 < n) {
            CHECK(st == FRAME_NEED_MORE);
        } else {
            CHECK(st == FRAME_OK);
            CHECK(strcmp(p, "{\"type\":\"PING\"}") == 0);
            got++;
        }
    }
    CHECK(got == 1);
    frame_decoder_free(&d);
}

static void test_many_frames_one_feed(void)
{
    frame_decoder_t d;
    frame_decoder_init(&d);
    uint8_t buf[512];
    size_t off = 0;
    const char *msgs[] = { "{\"n\":1}", "{\"n\":22}", "{\"n\":333}" };
    for (int i = 0; i < 3; i++)
        off += make_frame(buf + off, msgs[i]);
    CHECK(frame_decoder_feed(&d, buf, off) == 0);

    for (int i = 0; i < 3; i++) {
        const char *p; uint32_t len;
        CHECK(frame_decoder_next(&d, &p, &len) == FRAME_OK);
        CHECK(strcmp(p, msgs[i]) == 0);
        CHECK(len == strlen(msgs[i]));
    }
    const char *p; uint32_t len;
    CHECK(frame_decoder_next(&d, &p, &len) == FRAME_NEED_MORE);
    frame_decoder_free(&d);
}

static void test_frame_split_with_partial_next(void)
{
    /* 1 frame trọn + nửa frame thứ hai, rồi nửa còn lại */
    frame_decoder_t d;
    frame_decoder_init(&d);
    uint8_t buf[256];
    size_t a = make_frame(buf, "{\"first\":true}");
    size_t b = make_frame(buf + a, "{\"second\":true}");
    size_t cut = a + 6;                                  /* giữa frame 2 */
    CHECK(frame_decoder_feed(&d, buf, cut) == 0);

    const char *p; uint32_t len;
    CHECK(frame_decoder_next(&d, &p, &len) == FRAME_OK);
    CHECK(strcmp(p, "{\"first\":true}") == 0);
    CHECK(frame_decoder_next(&d, &p, &len) == FRAME_NEED_MORE);

    CHECK(frame_decoder_feed(&d, buf + cut, a + b - cut) == 0);
    CHECK(frame_decoder_next(&d, &p, &len) == FRAME_OK);
    CHECK(strcmp(p, "{\"second\":true}") == 0);
    frame_decoder_free(&d);
}

static void test_bad_lengths(void)
{
    frame_decoder_t d;
    const char *p; uint32_t len;
    uint32_t be;

    frame_decoder_init(&d);
    be = htonl(0);
    CHECK(frame_decoder_feed(&d, &be, 4) == 0);
    CHECK(frame_decoder_next(&d, &p, &len) == FRAME_ERROR);
    frame_decoder_free(&d);

    frame_decoder_init(&d);
    be = htonl(NET_MAX_PAYLOAD + 1);
    CHECK(frame_decoder_feed(&d, &be, 4) == 0);
    CHECK(frame_decoder_next(&d, &p, &len) == FRAME_ERROR);
    frame_decoder_free(&d);
}

static void test_max_payload_ok(void)
{
    frame_decoder_t d;
    frame_decoder_init(&d);
    char *payload = malloc(NET_MAX_PAYLOAD + 1);
    memset(payload, 'x', NET_MAX_PAYLOAD);
    payload[NET_MAX_PAYLOAD] = '\0';

    uint8_t *f = malloc(NET_HEADER_LEN + NET_MAX_PAYLOAD);
    ssize_t n = frame_encode(f, NET_HEADER_LEN + NET_MAX_PAYLOAD, payload,
                             NET_MAX_PAYLOAD);
    CHECK(n == (ssize_t)(NET_HEADER_LEN + NET_MAX_PAYLOAD));
    CHECK(frame_decoder_feed(&d, f, (size_t)n) == 0);

    const char *p; uint32_t len;
    CHECK(frame_decoder_next(&d, &p, &len) == FRAME_OK);
    CHECK(len == NET_MAX_PAYLOAD);
    CHECK(p[len] == '\0' && p[len - 1] == 'x');
    free(payload);
    free(f);
    frame_decoder_free(&d);
}

static void test_buffer_reuse_many_rounds(void)
{
    /* nhiều vòng feed/next liên tiếp: buffer không phình, nội dung đúng */
    frame_decoder_t d;
    frame_decoder_init(&d);
    for (int i = 0; i < 20000; i++) {
        char msg[64];
        snprintf(msg, sizeof msg, "{\"i\":%d}", i);
        uint8_t f[128];
        size_t n = make_frame(f, msg);
        /* chia đôi ngẫu nhiên để trộn các trường hợp */
        size_t cut = (size_t)(i % (int)n);
        CHECK(frame_decoder_feed(&d, f, cut) == 0);
        const char *p; uint32_t len;
        CHECK(frame_decoder_next(&d, &p, &len) == FRAME_NEED_MORE);
        CHECK(frame_decoder_feed(&d, f + cut, n - cut) == 0);
        CHECK(frame_decoder_next(&d, &p, &len) == FRAME_OK);
        if (strcmp(p, msg) != 0) {
            CHECK(0 && "nội dung sai");
            break;
        }
    }
    CHECK(d.cap <= 8192);
    frame_decoder_free(&d);
}

int main(void)
{
    struct { const char *name; void (*fn)(void); } tests[] = {
        { "encode khớp ví dụ trong protocol.md", test_encode_matches_spec },
        { "encode: giới hạn",                    test_encode_limits },
        { "một frame",                           test_single_frame },
        { "từng byte một (header bị cắt)",       test_byte_by_byte },
        { "nhiều frame trong một lần feed",      test_many_frames_one_feed },
        { "frame bị chia giữa chừng",            test_frame_split_with_partial_next },
        { "length = 0 / quá lớn",                test_bad_lengths },
        { "payload đúng 65536 byte",             test_max_payload_ok },
        { "20000 vòng, buffer không phình",      test_buffer_reuse_many_rounds },
    };
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; i++) {
        int before = failures;
        tests[i].fn();
        printf("[%s] %s\n", failures == before ? " ok " : "FAIL", tests[i].name);
    }
    printf("test_frame: %s\n", failures ? "CÓ LỖI" : "tất cả đạt");
    return failures ? 1 : 0;
}
