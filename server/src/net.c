/*
 * net.c - tầng mạng: epoll (level-triggered), framing, hàng đợi gửi, timeout.
 * Giao diện xem include/net.h. Một thread, một vòng epoll.
 *
 * Quy tắc sống còn của file này:
 *   - Không bao giờ free một net_conn_t giữa chừng một callback. Đóng kết nối
 *     chỉ là đánh dấu `dead`; việc dọn (on_close + free) làm ở reap_dead()
 *     vào cuối mỗi vòng lặp.
 *   - net_send chỉ đưa vào hàng đợi. send() thật chạy trong flush_pending()
 *     (cuối vòng lặp) hoặc khi epoll báo EPOLLOUT.
 */
#define _GNU_SOURCE
#include "net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define NET_DEFAULT_PORT  9000
#define NET_BACKLOG       128
#define NET_MAX_EVENTS    64
#define NET_RECV_CHUNK    16384
#define NET_READS_PER_EV  8          /* chia công bằng giữa các kết nối */
#define NET_TICK_MS       1000

/* Đệm nhận tối đa chưa xử lý: 1 frame đầy đủ + dư một chút. Chỉ để tự vệ. */
#define DEC_LIMIT         (NET_HEADER_LEN + NET_MAX_PAYLOAD + 65536u)

/* Hai thông điệp cố định mà net tự gửi (net không biết JSON nhưng giao thức
 * yêu cầu gửi trước khi đóng). Giữ đồng bộ với protocol.md mục 2, 4.4. */
static const char KICK_TIMEOUT_JSON[] =
    "{\"type\":\"EVT_KICKED\",\"data\":{\"reason\":\"timeout\"}}";
static const char KICK_SHUTDOWN_JSON[] =
    "{\"type\":\"EVT_KICKED\",\"data\":{\"reason\":\"server_shutdown\"}}";
static const char ERR_PROTOCOL_JSON[] =
    "{\"type\":\"ERROR_RES\",\"ok\":false,"
    "\"error\":{\"code\":10,\"message\":\"Lỗi giao thức: sai khung dữ liệu\"}}";

/* ================================================================
 * 1. Framing
 * ================================================================ */

void frame_decoder_init(frame_decoder_t *d)
{
    memset(d, 0, sizeof *d);
}

void frame_decoder_free(frame_decoder_t *d)
{
    free(d->buf);
    memset(d, 0, sizeof *d);
}

/* Trả lại byte đã bị ghi đè '\0' và coi frame trước đã được tiêu thụ. */
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

frame_status_t frame_decoder_next(frame_decoder_t *d,
                                  const char **payload, uint32_t *len)
{
    dec_release(d);

    size_t avail = d->len - d->off;
    if (avail < NET_HEADER_LEN) {
        if (avail == 0)
            d->off = d->len = 0;
        return FRAME_NEED_MORE;              /* header bị cắt */
    }

    uint32_t be;
    memcpy(&be, d->buf + d->off, sizeof be);   /* tránh lệch alignment */
    uint32_t n = ntohl(be);
    if (n == 0 || n > NET_MAX_PAYLOAD)
        return FRAME_ERROR;
    if (avail < NET_HEADER_LEN + (size_t)n)
        return FRAME_NEED_MORE;

    size_t start = d->off + NET_HEADER_LEN;
    size_t end   = start + n;                  /* end < cap nhờ chỗ '+1' */
    d->restore_pos  = end;
    d->restore_byte = d->buf[end];
    d->has_restore  = 1;
    d->buf[end]     = '\0';

    *payload = (const char *)d->buf + start;
    *len = n;
    return FRAME_OK;
}

ssize_t frame_encode(uint8_t *out, size_t out_cap,
                     const char *payload, uint32_t len)
{
    if (len == 0 || len > NET_MAX_PAYLOAD)
        return -1;
    if (out_cap < NET_HEADER_LEN + (size_t)len)
        return -1;
    uint32_t be = htonl(len);
    memcpy(out, &be, sizeof be);
    memcpy(out + NET_HEADER_LEN, payload, len);
    return (ssize_t)(NET_HEADER_LEN + len);
}

/* ================================================================
 * 2. Cấu trúc nội bộ
 * ================================================================ */

enum { TAG_CONN = 1, TAG_LISTEN = 2, TAG_WAKE = 3 };

struct net_conn {
    int              tag;          /* PHẢI là trường đầu: epoll data.ptr */
    int              fd;
    net_server_t    *srv;
    uint64_t         id;
    char             peer[64];

    frame_decoder_t  dec;

    uint8_t         *sendq;        /* header+payload đã mã hóa, nối đuôi nhau */
    size_t           sq_len;
    size_t           sq_off;       /* đã send tới đây */
    size_t           sq_cap;

    int64_t          connected_at; /* mili giây, đồng hồ monotonic */
    int64_t          last_recv;
    int64_t          rate_sec;     /* giây (cửa sổ rate limit) */
    unsigned         rate_cnt;

    bool             closing;      /* đóng sau khi gửi hết */
    bool             dead;         /* đã đánh dấu, chờ reap_dead() */
    bool             want_out;     /* đang đăng ký EPOLLOUT */
    bool             in_flush;     /* đang nằm trong flush list */

    void            *user;
    net_conn_t      *prev, *next;  /* danh sách mọi kết nối */
    net_conn_t      *flush_next;
};

struct net_server {
    int              tag_listen;   /* địa chỉ của chúng làm data.ptr */
    int              tag_wake;
    int              listen_fd;
    int              epfd;
    int              wake_fd;
    volatile sig_atomic_t stop;

    net_callbacks_t  cb;

    net_conn_t      *head;
    size_t           nconn;
    uint64_t         next_id;
    size_t           dead_count;
    net_conn_t      *flush_head;
    int64_t          last_tick;
};

static int64_t now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec;
}

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ================================================================
 * 3. Log
 * ================================================================ */

void net_log(const net_conn_t *c, const char *fmt, ...)
{
    char tbuf[32];
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(tbuf, sizeof tbuf, "%Y-%m-%d %H:%M:%S", &tm);

    char line[512];
    int off;
    if (c)
        off = snprintf(line, sizeof line, "%s conn=%llu ", tbuf,
                       (unsigned long long)c->id);
    else
        off = snprintf(line, sizeof line, "%s conn=- ", tbuf);

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line + off, sizeof line - (size_t)off, fmt, ap);
    va_end(ap);
    fprintf(stderr, "%s\n", line);
}

/* ================================================================
 * 4. Trạng thái kết nối: đánh dấu đóng, lên lịch flush, EPOLLOUT
 * ================================================================ */

static void mark_dead(net_conn_t *c)
{
    if (c->dead)
        return;
    c->dead = true;
    c->srv->dead_count++;
}

static void schedule_flush(net_conn_t *c)
{
    if (c->in_flush)
        return;
    c->in_flush = true;
    c->flush_next = c->srv->flush_head;
    c->srv->flush_head = c;
}

static void set_want_out(net_conn_t *c, bool on)
{
    if (c->want_out == on)
        return;
    struct epoll_event ev = {
        .events = EPOLLIN | (on ? EPOLLOUT : 0),
        .data.ptr = c,
    };
    if (epoll_ctl(c->srv->epfd, EPOLL_CTL_MOD, c->fd, &ev) < 0) {
        net_log(c, "epoll_ctl MOD lỗi: %s", strerror(errno));
        mark_dead(c);
        return;
    }
    c->want_out = on;
}

/* Gửi càng nhiều càng tốt. Hết hàng đợi thì tắt EPOLLOUT (và đóng nếu closing). */
static void flush_conn(net_conn_t *c)
{
    while (c->sq_off < c->sq_len) {
        ssize_t n = send(c->fd, c->sendq + c->sq_off, c->sq_len - c->sq_off,
                         MSG_NOSIGNAL);
        if (n > 0) {
            c->sq_off += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            set_want_out(c, true);       /* chờ socket ghi được lại */
            return;
        }
        net_log(c, "send lỗi: %s", n < 0 ? strerror(errno) : "0 byte");
        mark_dead(c);
        return;
    }

    c->sq_off = c->sq_len = 0;
    if (c->sq_cap > 64 * 1024) {         /* trả bộ nhớ sau cơn bùng nổ */
        free(c->sendq);
        c->sendq = NULL;
        c->sq_cap = 0;
    }
    set_want_out(c, false);
    if (c->closing)
        mark_dead(c);
}

static void flush_pending(net_server_t *s)
{
    net_conn_t *c = s->flush_head;
    s->flush_head = NULL;
    while (c) {
        net_conn_t *nx = c->flush_next;
        c->in_flush = false;
        c->flush_next = NULL;
        if (!c->dead)
            flush_conn(c);
        c = nx;
    }
}

/* ================================================================
 * 5. API trên kết nối
 * ================================================================ */

uint64_t net_conn_id(const net_conn_t *c)         { return c->id; }
const char *net_conn_peer(const net_conn_t *c)    { return c->peer; }
void net_conn_set_data(net_conn_t *c, void *p)    { c->user = p; }
void *net_conn_get_data(const net_conn_t *c)      { return c->user; }

int net_conn_rate_check(net_conn_t *c, unsigned max_per_sec)
{
    int64_t sec = now_s();
    if (sec != c->rate_sec) {
        c->rate_sec = sec;
        c->rate_cnt = 0;
    }
    return ++c->rate_cnt <= max_per_sec;
}

int net_send(net_conn_t *c, const char *payload, uint32_t len)
{
    if (!c || c->dead || c->closing)
        return -1;
    if (!payload || len == 0 || len > NET_MAX_PAYLOAD)
        return -1;

    size_t pending = c->sq_len - c->sq_off;
    if (pending + NET_HEADER_LEN + len > NET_SENDQ_MAX) {
        net_log(c, "hàng đợi gửi vượt %u byte (client đọc quá chậm), đóng",
                (unsigned)NET_SENDQ_MAX);
        mark_dead(c);
        return -2;
    }

    size_t need = c->sq_len + NET_HEADER_LEN + len;
    if (need > c->sq_cap && c->sq_off > 0) {   /* thu hồi phần đã gửi */
        memmove(c->sendq, c->sendq + c->sq_off, pending);
        c->sq_len = pending;
        c->sq_off = 0;
        need = c->sq_len + NET_HEADER_LEN + len;
    }
    if (need > c->sq_cap) {
        size_t cap = c->sq_cap ? c->sq_cap : 4096;
        while (cap < need)
            cap *= 2;
        uint8_t *nb = realloc(c->sendq, cap);
        if (!nb)
            return -1;
        c->sendq = nb;
        c->sq_cap = cap;
    }

    ssize_t w = frame_encode(c->sendq + c->sq_len, c->sq_cap - c->sq_len,
                             payload, len);
    if (w < 0)
        return -1;
    c->sq_len += (size_t)w;
    schedule_flush(c);
    return 0;
}

void net_close_after_flush(net_conn_t *c)
{
    if (!c || c->dead || c->closing)
        return;
    c->closing = true;
    if (c->sq_off == c->sq_len)
        mark_dead(c);
    else
        schedule_flush(c);
}

void net_close_now(net_conn_t *c)
{
    if (c)
        mark_dead(c);
}

/* ================================================================
 * 6. Nhận dữ liệu
 * ================================================================ */

static void protocol_error(net_conn_t *c)
{
    net_log(c, "lỗi giao thức (framing), gửi ERR_PROTOCOL rồi đóng");
    net_send(c, ERR_PROTOCOL_JSON, sizeof ERR_PROTOCOL_JSON - 1);
    net_close_after_flush(c);
}

static void handle_read(net_conn_t *c)
{
    net_server_t *s = c->srv;
    uint8_t buf[NET_RECV_CHUNK];

    for (int i = 0; i < NET_READS_PER_EV && !c->dead; i++) {
        ssize_t n = recv(c->fd, buf, sizeof buf, 0);
        if (n == 0) {
            net_log(c, "client đóng kết nối");
            mark_dead(c);
            return;
        }
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return;
            net_log(c, "recv lỗi: %s", strerror(errno));
            mark_dead(c);
            return;
        }

        c->last_recv = now_ms();
        if (c->closing)
            continue;                    /* đang đóng: bỏ dữ liệu vào */

        if (frame_decoder_feed(&c->dec, buf, (size_t)n) < 0) {
            net_log(c, "không cấp được bộ đệm nhận, đóng");
            mark_dead(c);
            return;
        }

        for (;;) {
            const char *p;
            uint32_t len;
            frame_status_t st = frame_decoder_next(&c->dec, &p, &len);
            if (st == FRAME_NEED_MORE)
                break;
            if (st == FRAME_ERROR) {
                protocol_error(c);
                return;
            }
            s->cb.on_payload(c, p, len, s->cb.userdata);
            if (c->dead || c->closing)
                break;
        }

        if ((size_t)n < sizeof buf)
            return;                      /* đã hết dữ liệu; LT sẽ báo lại nếu còn */
    }
}

/* ================================================================
 * 7. Accept, giải phóng, timeout
 * ================================================================ */

static void accept_all(net_server_t *s)
{
    for (;;) {
        struct sockaddr_in addr;
        socklen_t alen = sizeof addr;
        int fd = accept4(s->listen_fd, (struct sockaddr *)&addr, &alen,
                         SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) {
            if (errno == EINTR)
                continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK)
                net_log(NULL, "accept lỗi: %s", strerror(errno));
            return;
        }

        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);

        net_conn_t *c = calloc(1, sizeof *c);
        if (!c) {
            close(fd);
            continue;
        }
        c->tag = TAG_CONN;
        c->fd = fd;
        c->srv = s;
        c->id = ++s->next_id;
        c->connected_at = c->last_recv = now_ms();
        frame_decoder_init(&c->dec);
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof ip);
        snprintf(c->peer, sizeof c->peer, "%s:%u", ip, ntohs(addr.sin_port));

        struct epoll_event ev = { .events = EPOLLIN, .data.ptr = c };
        if (epoll_ctl(s->epfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
            net_log(NULL, "epoll_ctl ADD lỗi: %s", strerror(errno));
            close(fd);
            free(c);
            continue;
        }

        c->next = s->head;
        if (s->head)
            s->head->prev = c;
        s->head = c;
        s->nconn++;

        net_log(c, "kết nối mới từ %s (tổng %zu)", c->peer, s->nconn);
        if (s->cb.on_connect)
            s->cb.on_connect(c, s->cb.userdata);
    }
}

/* Dọn các kết nối đã đánh dấu dead: on_close đúng một lần rồi free. */
static void reap_dead(net_server_t *s)
{
    while (s->dead_count > 0) {
        net_conn_t *c = s->head;
        while (c) {
            net_conn_t *nx = c->next;
            if (!c->dead) {
                c = nx;
                continue;
            }
            s->dead_count--;

            if (c->prev) c->prev->next = c->next; else s->head = c->next;
            if (c->next) c->next->prev = c->prev;
            s->nconn--;

            epoll_ctl(s->epfd, EPOLL_CTL_DEL, c->fd, NULL);
            close(c->fd);

            net_log(c, "đóng kết nối (còn %zu)", s->nconn);
            if (s->cb.on_close)
                s->cb.on_close(c, s->cb.userdata);

            frame_decoder_free(&c->dec);
            free(c->sendq);
            free(c);
            c = nx;
        }
    }
}

/* Gửi EVT_KICKED(timeout) rồi đóng sau khi gửi xong. */
static void kick_timeout(net_conn_t *c, const char *why)
{
    net_log(c, "%s, đóng kết nối", why);
    net_send(c, KICK_TIMEOUT_JSON, sizeof KICK_TIMEOUT_JSON - 1);
    net_close_after_flush(c);
}

static void check_timeouts(net_server_t *s)
{
    int64_t now = now_ms();
    for (net_conn_t *c = s->head; c; c = c->next) {
        if (c->dead || c->closing)
            continue;
        int authed = s->cb.is_authed ? s->cb.is_authed(c, s->cb.userdata) : 0;
        if (!authed && now - c->connected_at >= NET_AUTH_TIMEOUT_S * 1000LL)
            kick_timeout(c, "quá hạn chờ đăng nhập");
        else if (now - c->last_recv >= NET_IDLE_TIMEOUT_S * 1000LL)
            kick_timeout(c, "không nhận được dữ liệu quá hạn");
    }
}

/* ================================================================
 * 8. Vòng đời server
 * ================================================================ */

net_server_t *net_server_create(uint16_t port, const net_callbacks_t *cb)
{
    if (!cb || !cb->on_payload) {
        errno = EINVAL;
        return NULL;
    }
    if (port == 0)
        port = NET_DEFAULT_PORT;

    signal(SIGPIPE, SIG_IGN);            /* thêm một lớp bảo vệ ngoài MSG_NOSIGNAL */

    net_server_t *s = calloc(1, sizeof *s);
    if (!s)
        return NULL;
    s->tag_listen = TAG_LISTEN;
    s->tag_wake = TAG_WAKE;
    s->cb = *cb;
    s->listen_fd = s->epfd = s->wake_fd = -1;

    s->listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (s->listen_fd < 0)
        goto fail;
    int one = 1;
    setsockopt(s->listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(port),
    };
    if (bind(s->listen_fd, (struct sockaddr *)&addr, sizeof addr) < 0)
        goto fail;
    if (listen(s->listen_fd, NET_BACKLOG) < 0)
        goto fail;

    s->epfd = epoll_create1(EPOLL_CLOEXEC);
    if (s->epfd < 0)
        goto fail;
    s->wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (s->wake_fd < 0)
        goto fail;

    struct epoll_event ev = { .events = EPOLLIN, .data.ptr = &s->tag_listen };
    if (epoll_ctl(s->epfd, EPOLL_CTL_ADD, s->listen_fd, &ev) < 0)
        goto fail;
    ev.data.ptr = &s->tag_wake;
    if (epoll_ctl(s->epfd, EPOLL_CTL_ADD, s->wake_fd, &ev) < 0)
        goto fail;

    s->last_tick = now_ms();
    net_log(NULL, "lắng nghe trên cổng %u", (unsigned)port);
    return s;

fail: {
        int saved = errno;
        net_log(NULL, "khởi tạo server lỗi: %s", strerror(saved));
        if (s->listen_fd >= 0) close(s->listen_fd);
        if (s->epfd >= 0)      close(s->epfd);
        if (s->wake_fd >= 0)   close(s->wake_fd);
        free(s);
        errno = saved;
        return NULL;
    }
}

static void handle_event(net_server_t *s, struct epoll_event *ev)
{
    int tag = *(int *)ev->data.ptr;

    if (tag == TAG_LISTEN) {
        accept_all(s);
        return;
    }
    if (tag == TAG_WAKE) {
        uint64_t v;
        if (read(s->wake_fd, &v, sizeof v) < 0) { /* chỉ để làm rỗng eventfd */ }
        return;
    }

    net_conn_t *c = ev->data.ptr;
    if (c->dead)
        return;
    if (ev->events & EPOLLIN)
        handle_read(c);
    else if (ev->events & (EPOLLERR | EPOLLHUP)) {
        mark_dead(c);
        return;
    }
    if (!c->dead && (ev->events & EPOLLOUT))
        flush_conn(c);
}

int net_server_run(net_server_t *s)
{
    struct epoll_event events[NET_MAX_EVENTS];

    while (!s->stop) {
        int timeout = NET_TICK_MS - (int)(now_ms() - s->last_tick);
        if (timeout < 0)
            timeout = 0;
        if (s->flush_head || s->dead_count)
            timeout = 0;

        int n = epoll_wait(s->epfd, events, NET_MAX_EVENTS, timeout);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            net_log(NULL, "epoll_wait lỗi: %s", strerror(errno));
            return -1;
        }
        for (int i = 0; i < n; i++)
            handle_event(s, &events[i]);

        if (now_ms() - s->last_tick >= NET_TICK_MS) {
            s->last_tick = now_ms();
            check_timeouts(s);
            if (s->cb.on_tick)
                s->cb.on_tick(s->cb.userdata);
        }

        /* Callback có thể sinh thêm việc gửi/đóng: lặp tới khi yên. */
        while (s->flush_head || s->dead_count) {
            flush_pending(s);
            reap_dead(s);
        }
    }
    return 0;
}

void net_server_stop(net_server_t *s)
{
    s->stop = 1;
    uint64_t one = 1;
    ssize_t r = write(s->wake_fd, &one, sizeof one);   /* async-signal-safe */
    (void)r;
}

void net_server_destroy(net_server_t *s)
{
    if (!s)
        return;

    /* Báo trước rồi gửi một lượt (không chặn); sau đó đóng hết. */
    for (net_conn_t *c = s->head; c; c = c->next) {
        if (c->dead)
            continue;
        if (!c->closing)
            net_send(c, KICK_SHUTDOWN_JSON, sizeof KICK_SHUTDOWN_JSON - 1);
        flush_conn(c);
    }
    s->flush_head = NULL;
    for (net_conn_t *c = s->head; c; c = c->next) {
        c->in_flush = false;
        c->flush_next = NULL;
        mark_dead(c);
    }
    reap_dead(s);

    close(s->listen_fd);
    close(s->wake_fd);
    close(s->epfd);
    free(s);
}
