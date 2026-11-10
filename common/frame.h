/*
 * frame.h - khung dữ liệu trên TCP (protocol-v2.md mục 2).
 *
 *   +--------------+-------------+-------------+--------------------------+
 *   | length (4 B) | seq (4 B)   | ack (4 B)   | payload (length byte)    |
 *   | uint32 BE    | uint32 BE   | uint32 BE   | JSON UTF-8, có thể đã mã |
 *   +--------------+-------------+-------------+--------------------------+
 *
 * length chỉ tính payload (1..65536). seq: số thứ tự khung trong chiều gửi của
 * kết nối (từ 1, modulo 2^32). ack: seq lớn nhất đã nhận từ bên kia (0 = chưa có).
 *
 * Thuần túy xử lý byte, không socket: dùng chung cho server, lõi client và bot,
 * và unit test được riêng. Mã hóa payload nằm ở cipher.h, tầng gọi tự áp dụng.
 */
#ifndef FRAME_H
#define FRAME_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>   /* ssize_t */

#define FRAME_HEADER_LEN   12u
#define FRAME_MAX_PAYLOAD  65536u

typedef enum {
    FRAME_NEED_MORE = 0,   /* chưa đủ byte, chờ recv tiếp */
    FRAME_OK        = 1,   /* đã lấy được 1 khung */
    FRAME_ERROR     = -1   /* length == 0 hoặc > FRAME_MAX_PAYLOAD */
} frame_status_t;

/*
 * Bộ đệm nhận của một kết nối. Chịu được header bị cắt (kể cả mới nhận 5/12 byte)
 * và nhiều khung trong một lần recv. Các trường là nội bộ.
 */
typedef struct {
    uint8_t *buf;
    size_t   len;          /* số byte đang có */
    size_t   cap;
    size_t   off;          /* đầu vùng chưa xử lý */
    size_t   restore_pos;  /* vị trí byte bị ghi đè bằng '\0' ... */
    uint8_t  restore_byte; /* ... và giá trị gốc của nó */
    int      has_restore;
} frame_decoder_t;

void frame_decoder_init(frame_decoder_t *d);
void frame_decoder_free(frame_decoder_t *d);

/* Thêm byte vừa recv được. Trả 0 nếu ok, -1 nếu hết bộ nhớ hoặc đệm quá giới hạn. */
int  frame_decoder_feed(frame_decoder_t *d, const void *data, size_t n);

/*
 * Thử lấy 1 khung. Gọi lặp tới khi != FRAME_OK.
 * FRAME_OK: *payload trỏ vào bên trong buffer của decoder và GHI ĐƯỢC (để giải mã
 *           tại chỗ), *len là độ dài; payload luôn có '\0' ngay sau. Con trỏ chỉ
 *           hợp lệ tới lần gọi feed/next kế tiếp. seq/ack có thể là NULL.
 */
frame_status_t frame_decoder_next(frame_decoder_t *d, char **payload,
                                  uint32_t *len, uint32_t *seq, uint32_t *ack);

/*
 * Ghi header + payload vào out (cần FRAME_HEADER_LEN + len byte). Trả tổng số
 * byte ghi, hoặc -1 nếu len ngoài 1..FRAME_MAX_PAYLOAD hoặc out quá nhỏ.
 */
ssize_t frame_encode(uint8_t *out, size_t out_cap, const char *payload,
                     uint32_t len, uint32_t seq, uint32_t ack);

#endif /* FRAME_H */
