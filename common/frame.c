/* frame.c - ghép/tách khung 12 byte (xem frame.h). */
#include "frame.h"

#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>

/* Đệm nhận tối đa chưa xử lý: 1 khung đầy đủ + dư một chút. Chỉ để tự vệ. */
#define DEC_LIMIT (FRAME_HEADER_LEN + FRAME_MAX_PAYLOAD + 65536u)

void frame_decoder_init(frame_decoder_t *d)
{
    memset(d, 0, sizeof *d);
}

void frame_decoder_free(frame_decoder_t *d)
{
    free(d->buf);
    memset(d, 0, sizeof *d);
}

/* Trả lại byte đã bị ghi đè '\0' và coi khung trước đã được tiêu thụ. */
static void dec_release(frame_decoder_t *d)
{
    if (d->has_restore) {
        d->buf[d->restore_pos] = d->restore_byte;
        d->off = d->restore_pos;
        d->has_restore = 0;
    }
}

static void dec_compact(frame_decoder_t *d)
{
    if (d->off == 0)
        return;
    if (d->off < d->len)
        memmove(d->buf, d->buf + d->off, d->len - d->off);
    d->len -= d->off;
    d->off = 0;
}

int frame_decoder_feed(frame_decoder_t *d, const void *data, size_t n)
{
    dec_release(d);
    dec_compact(d);
    if (n == 0)
        return 0;
    if (d->len + n > DEC_LIMIT)
        return -1;

    size_t need = d->len + n + 1;          /* +1 chỗ cho '\0' sau payload */
    if (need > d->cap) {
        size_t cap = d->cap ? d->cap : 4096;
        while (cap < need)
            cap *= 2;
        uint8_t *nb = realloc(d->buf, cap);
        if (!nb)
            return -1;
        d->buf = nb;
        d->cap = cap;
    }
    memcpy(d->buf + d->len, data, n);
    d->len += n;
    return 0;
}

static uint32_t get_be32(const uint8_t *p)
{
    uint32_t be;
    memcpy(&be, p, sizeof be);             /* tránh lệch alignment */
    return ntohl(be);
}

frame_status_t frame_decoder_next(frame_decoder_t *d, char **payload,
                                  uint32_t *len, uint32_t *seq, uint32_t *ack)
{
    dec_release(d);

    size_t avail = d->len - d->off;
    if (avail < FRAME_HEADER_LEN) {
        if (avail == 0)
            d->off = d->len = 0;
        return FRAME_NEED_MORE;              /* header bị cắt */
    }

    const uint8_t *h = d->buf + d->off;
    uint32_t n = get_be32(h);
    if (n == 0 || n > FRAME_MAX_PAYLOAD)
        return FRAME_ERROR;
    if (avail < FRAME_HEADER_LEN + (size_t)n)
        return FRAME_NEED_MORE;

    if (seq)
        *seq = get_be32(h + 4);
    if (ack)
        *ack = get_be32(h + 8);

    size_t start = d->off + FRAME_HEADER_LEN;
    size_t end   = start + n;                /* end < cap nhờ chỗ '+1' */
    d->restore_pos  = end;
    d->restore_byte = d->buf[end];
    d->has_restore  = 1;
    d->buf[end]     = '\0';

    *payload = (char *)d->buf + start;
    *len = n;
    return FRAME_OK;
}

ssize_t frame_encode(uint8_t *out, size_t out_cap, const char *payload,
                     uint32_t len, uint32_t seq, uint32_t ack)
{
    if (len == 0 || len > FRAME_MAX_PAYLOAD)
        return -1;
    if (out_cap < FRAME_HEADER_LEN + (size_t)len)
        return -1;
    uint32_t be[3] = { htonl(len), htonl(seq), htonl(ack) };
    memcpy(out, be, sizeof be);
    memcpy(out + FRAME_HEADER_LEN, payload, len);
    return (ssize_t)(FRAME_HEADER_LEN + len);
}
