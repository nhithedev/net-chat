/* Unit test cho khung 12 byte (frame.c) và mã hóa Caesar (cipher.c), không dùng socket. */
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cipher.h"
#include "frame.h"

static int failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            failures++;                                                    \
        }                                                                  \
    } while (0)

static size_t make_frame(uint8_t *out, const char *payload, uint32_t seq, uint32_t ack)
{
    return (size_t)frame_encode(out, 70000, payload, (uint32_t)strlen(payload), seq, ack);
}

/* ---------------------------------------------------------------- khung */

static void test_encode_matches_spec(void)
{
    /* Ví dụ trong protocol-v2.md mục 2: PING, seq = 5, ack = 4, dạng rõ. */
    const char *ping = "{\"type\":\"PING\",\"id\":\"k-1\"}";
    CHECK(strlen(ping) == 26);

    uint8_t out[64];
    ssize_t n = frame_encode(out, sizeof out, ping, 26, 5, 4);
    static const uint8_t want_header[12] = {
        0x00, 0x00, 0x00, 0x1A, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x04 };
    CHECK(n == 12 + 26);
    CHECK(memcmp(out, want_header, 12) == 0);
    CHECK(memcmp(out + 12, ping, 26) == 0);

    /* Cùng khung sau khi mã hóa payload với khóa 3 (header giữ nguyên). */
    cipher_encode(out + 12, 26);
    static const uint8_t want_enc[26] = {
        0x7E, 0x25, 0x77, 0x7C, 0x73, 0x68, 0x25, 0x3D, 0x25, 0x53, 0x4C, 0x51, 0x4A,
        0x25, 0x2F, 0x25, 0x6C, 0x67, 0x25, 0x3D, 0x25, 0x6E, 0x30, 0x34, 0x25, 0x80 };
    CHECK(memcmp(out, want_header, 12) == 0);
    CHECK(memcmp(out + 12, want_enc, 26) == 0);
}

static void test_encode_limits(void)
{
    uint8_t out[32];
    CHECK(frame_encode(out, sizeof out, "x", 0, 1, 0) == -1);
    CHECK(frame_encode(out, sizeof out, "x", FRAME_MAX_PAYLOAD + 1, 1, 0) == -1);
    CHECK(frame_encode(out, 13, "ab", 2, 1, 0) == -1);          /* out quá nhỏ */
    CHECK(frame_encode(out, 14, "ab", 2, 1, 0) == 14);          /* khung nhỏ nhất là 13 + 1 */
    CHECK(frame_encode(out, 13, "a", 1, 1, 0) == 13);
}

static void test_single_frame(void)
{
    frame_decoder_t d;
    frame_decoder_init(&d);
    uint8_t f[128];
    size_t n = make_frame(f, "{\"a\":1}", 7, 3);
    CHECK(frame_decoder_feed(&d, f, n) == 0);

    char *p; uint32_t len, seq, ack;
    CHECK(frame_decoder_next(&d, &p, &len, &seq, &ack) == FRAME_OK);
    CHECK(len == 7 && seq == 7 && ack == 3);
    CHECK(strcmp(p, "{\"a\":1}") == 0);                  /* NUL-terminated */
    CHECK(frame_decoder_next(&d, &p, &len, &seq, &ack) == FRAME_NEED_MORE);
    frame_decoder_free(&d);
}

static void test_byte_by_byte(void)
{
    /* header 12 byte bị cắt ở mọi vị trí */
    frame_decoder_t d;
    frame_decoder_init(&d);
    uint8_t f[128];
    size_t n = make_frame(f, "{\"type\":\"PING\"}", 1, 0);
    int got = 0;
    for (size_t i = 0; i < n; i++) {
        CHECK(frame_decoder_feed(&d, f + i, 1) == 0);
        char *p; uint32_t len, seq, ack;
        frame_status_t st = frame_decoder_next(&d, &p, &len, &seq, &ack);
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
    for (uint32_t i = 0; i < 3; i++)
        off += make_frame(buf + off, msgs[i], i + 1, i);
    CHECK(frame_decoder_feed(&d, buf, off) == 0);

    for (uint32_t i = 0; i < 3; i++) {
        char *p; uint32_t len, seq, ack;
        CHECK(frame_decoder_next(&d, &p, &len, &seq, &ack) == FRAME_OK);
        CHECK(strcmp(p, msgs[i]) == 0);
        CHECK(len == strlen(msgs[i]));
        CHECK(seq == i + 1 && ack == i);
    }
    char *p; uint32_t len;
    CHECK(frame_decoder_next(&d, &p, &len, NULL, NULL) == FRAME_NEED_MORE);
    frame_decoder_free(&d);
}

static void test_frame_split_with_partial_next(void)
{
    /* 1 khung trọn + nửa khung thứ hai (giữa header), rồi nửa còn lại */
    frame_decoder_t d;
    frame_decoder_init(&d);
    uint8_t buf[256];
    size_t a = make_frame(buf, "{\"first\":true}", 1, 0);
    size_t b = make_frame(buf + a, "{\"second\":true}", 2, 1);
    size_t cut = a + 5;                                  /* mới nhận 5/12 byte header khung 2 */
    CHECK(frame_decoder_feed(&d, buf, cut) == 0);

    char *p; uint32_t len;
    CHECK(frame_decoder_next(&d, &p, &len, NULL, NULL) == FRAME_OK);
    CHECK(strcmp(p, "{\"first\":true}") == 0);
    CHECK(frame_decoder_next(&d, &p, &len, NULL, NULL) == FRAME_NEED_MORE);

    CHECK(frame_decoder_feed(&d, buf + cut, a + b - cut) == 0);
    CHECK(frame_decoder_next(&d, &p, &len, NULL, NULL) == FRAME_OK);
    CHECK(strcmp(p, "{\"second\":true}") == 0);
    frame_decoder_free(&d);
}

static void test_bad_lengths(void)
{
    char *p; uint32_t len;
    uint32_t hdr[3];

    frame_decoder_t d;
    frame_decoder_init(&d);
    hdr[0] = htonl(0); hdr[1] = htonl(1); hdr[2] = htonl(0);
    CHECK(frame_decoder_feed(&d, hdr, sizeof hdr) == 0);
    CHECK(frame_decoder_next(&d, &p, &len, NULL, NULL) == FRAME_ERROR);
    frame_decoder_free(&d);

    frame_decoder_init(&d);
    hdr[0] = htonl(FRAME_MAX_PAYLOAD + 1);
    CHECK(frame_decoder_feed(&d, hdr, sizeof hdr) == 0);
    CHECK(frame_decoder_next(&d, &p, &len, NULL, NULL) == FRAME_ERROR);
    frame_decoder_free(&d);

    /* header chưa đủ 12 byte thì chưa thể kết luận: chờ thêm */
    frame_decoder_init(&d);
    hdr[0] = htonl(0);
    CHECK(frame_decoder_feed(&d, hdr, 4) == 0);
    CHECK(frame_decoder_next(&d, &p, &len, NULL, NULL) == FRAME_NEED_MORE);
    frame_decoder_free(&d);
}

static void test_max_payload_ok(void)
{
    frame_decoder_t d;
    frame_decoder_init(&d);
    char *payload = malloc(FRAME_MAX_PAYLOAD + 1);
    memset(payload, 'x', FRAME_MAX_PAYLOAD);
    payload[FRAME_MAX_PAYLOAD] = '\0';

    uint8_t *f = malloc(FRAME_HEADER_LEN + FRAME_MAX_PAYLOAD);
    ssize_t n = frame_encode(f, FRAME_HEADER_LEN + FRAME_MAX_PAYLOAD, payload,
                             FRAME_MAX_PAYLOAD, 1, 0);
    CHECK(n == (ssize_t)(FRAME_HEADER_LEN + FRAME_MAX_PAYLOAD));
    CHECK(frame_decoder_feed(&d, f, (size_t)n) == 0);

    char *p; uint32_t len;
    CHECK(frame_decoder_next(&d, &p, &len, NULL, NULL) == FRAME_OK);
    CHECK(len == FRAME_MAX_PAYLOAD);
    CHECK(p[len] == '\0' && p[len - 1] == 'x');
    free(payload);
    free(f);
    frame_decoder_free(&d);
}

static void test_seq_wraps_modulo_2_32(void)
{
    frame_decoder_t d;
    frame_decoder_init(&d);
    uint8_t f[64];
    size_t n = make_frame(f, "{}", 0xFFFFFFFFu, 0);
    CHECK(frame_decoder_feed(&d, f, n) == 0);
    char *p; uint32_t len, seq, ack;
    CHECK(frame_decoder_next(&d, &p, &len, &seq, &ack) == FRAME_OK);
    CHECK(seq == 0xFFFFFFFFu);
    CHECK((uint32_t)(seq + 1) == 0);
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
        size_t n = make_frame(f, msg, (uint32_t)i + 1, 0);
        /* chia đôi ở mọi vị trí (kể cả giữa header) để trộn các trường hợp */
        size_t cut = (size_t)(i % (int)n);
        CHECK(frame_decoder_feed(&d, f, cut) == 0);
        char *p; uint32_t len, seq;
        CHECK(frame_decoder_next(&d, &p, &len, &seq, NULL) == FRAME_NEED_MORE);
        CHECK(frame_decoder_feed(&d, f + cut, n - cut) == 0);
        CHECK(frame_decoder_next(&d, &p, &len, &seq, NULL) == FRAME_OK);
        if (strcmp(p, msg) != 0 || seq != (uint32_t)i + 1) {
            CHECK(0 && "nội dung sai");
            break;
        }
    }
    CHECK(d.cap <= 8192);
    frame_decoder_free(&d);
}

/* ---------------------------------------------------------------- Caesar */

static void test_cipher_all_byte_values(void)
{
    uint8_t buf[256], orig[256];
    for (int i = 0; i < 256; i++)
        orig[i] = buf[i] = (uint8_t)i;
    cipher_encode(buf, sizeof buf);
    CHECK(buf[0] == 3 && buf[253] == 0 && buf[255] == 2);       /* quay vòng modulo 256 */
    CHECK(memcmp(buf, orig, sizeof buf) != 0);
    cipher_decode(buf, sizeof buf);
    CHECK(memcmp(buf, orig, sizeof buf) == 0);
}

static void test_cipher_empty_and_vietnamese(void)
{
    cipher_encode(NULL, 0);                                     /* chuỗi rỗng: không làm gì */
    cipher_decode(NULL, 0);

    char text[] = "{\"text\":\"Chào Bình, họp lúc 9 giờ nhé ✓\"}";
    char copy[sizeof text];
    memcpy(copy, text, sizeof text);
    size_t n = strlen(text);
    cipher_encode((uint8_t *)copy, n);
    CHECK(memcmp(copy, text, n) != 0);
    cipher_decode((uint8_t *)copy, n);
    CHECK(strcmp(copy, text) == 0);                             /* UTF-8 nguyên vẹn */
}

static void test_cipher_max_payload_through_frame(void)
{
    /* Gửi: khung -> mã hóa payload. Nhận: decoder -> giải mã tại chỗ. */
    char *payload = malloc(FRAME_MAX_PAYLOAD + 1);
    for (size_t i = 0; i < FRAME_MAX_PAYLOAD; i++)
        payload[i] = (char)('a' + i % 26);
    payload[FRAME_MAX_PAYLOAD] = '\0';

    uint8_t *f = malloc(FRAME_HEADER_LEN + FRAME_MAX_PAYLOAD);
    ssize_t n = frame_encode(f, FRAME_HEADER_LEN + FRAME_MAX_PAYLOAD, payload,
                             FRAME_MAX_PAYLOAD, 1, 0);
    cipher_encode(f + FRAME_HEADER_LEN, FRAME_MAX_PAYLOAD);

    frame_decoder_t d;
    frame_decoder_init(&d);
    CHECK(frame_decoder_feed(&d, f, (size_t)n) == 0);
    char *p; uint32_t len;
    CHECK(frame_decoder_next(&d, &p, &len, NULL, NULL) == FRAME_OK);
    CHECK(len == FRAME_MAX_PAYLOAD);
    CHECK(memcmp(p, payload, len) != 0);                        /* trên dây khác bản gốc */
    cipher_decode((uint8_t *)p, len);
    CHECK(memcmp(p, payload, len) == 0);
    CHECK(p[len] == '\0');                                      /* dấu kết thúc không bị đổi */
    frame_decoder_free(&d);
    free(payload);
    free(f);
}

int main(void)
{
    struct { const char *name; void (*fn)(void); } tests[] = {
        { "encode khớp ví dụ PING (rõ và mã hóa)", test_encode_matches_spec },
        { "encode: giới hạn",                      test_encode_limits },
        { "một khung (seq/ack)",                   test_single_frame },
        { "từng byte một (header 12 byte bị cắt)", test_byte_by_byte },
        { "nhiều khung trong một lần feed",        test_many_frames_one_feed },
        { "khung bị chia giữa header",             test_frame_split_with_partial_next },
        { "length = 0 / quá lớn / header dở",      test_bad_lengths },
        { "payload đúng 65536 byte",               test_max_payload_ok },
        { "seq quay vòng modulo 2^32",             test_seq_wraps_modulo_2_32 },
        { "20000 vòng, buffer không phình",        test_buffer_reuse_many_rounds },
        { "Caesar: mọi giá trị byte",              test_cipher_all_byte_values },
        { "Caesar: rỗng và tiếng Việt",            test_cipher_empty_and_vietnamese },
        { "Caesar: payload 65536 qua khung",       test_cipher_max_payload_through_frame },
    };
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; i++) {
        int before = failures;
        tests[i].fn();
        printf("[%s] %s\n", failures == before ? " ok " : "FAIL", tests[i].name);
    }
    printf("test_frame: %s\n", failures ? "CÓ LỖI" : "tất cả đạt");
    return failures ? 1 : 0;
}
