/*
 * client.c - client dòng lệnh để thử server bằng tay (không phải client thật
 * của ứng dụng; client thật là React qua gateway).
 *
 * Dùng: netchat-client [host] [port]     (mặc định 127.0.0.1 9000)
 *
 * Gõ vào một dòng rồi Enter:
 *   {"type":"PING","id":"c-1"}   gửi nguyên dòng JSON đó (tự thêm 4 byte độ dài)
 *   /ping                        gửi PING với id tự sinh
 *   /quit  (hoặc Ctrl+D)         thoát
 * Mọi message server gửi về được in ra dạng "<< {...}".
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "net.h"            /* chỉ dùng phần framing: frame_decoder_*, frame_encode */

static int connect_to(const char *host, const char *port)
{
    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
    struct addrinfo *res;
    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        return -1;
    }
    int fd = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0)
            continue;
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0)
        fprintf(stderr, "không kết nối được tới %s:%s: %s\n", host, port,
                strerror(errno));
    return fd;
}

/* Gửi hết n byte (socket blocking, client đơn giản nên chấp nhận chặn). */
static int send_all(int fd, const uint8_t *p, size_t n)
{
    while (n > 0) {
        ssize_t w = send(fd, p, n, MSG_NOSIGNAL);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

static int send_payload(int fd, const char *payload, size_t len)
{
    static uint8_t out[NET_HEADER_LEN + NET_MAX_PAYLOAD];
    ssize_t n = frame_encode(out, sizeof out, payload, (uint32_t)len);
    if (n < 0) {
        fprintf(stderr, "payload phải dài 1..%u byte\n", NET_MAX_PAYLOAD);
        return 0;                        /* lỗi nhập liệu, không phải lỗi mạng */
    }
    return send_all(fd, out, (size_t)n);
}

int main(int argc, char **argv)
{
    const char *host = argc > 1 ? argv[1] : "127.0.0.1";
    const char *port = argc > 2 ? argv[2] : "9000";

    int fd = connect_to(host, port);
    if (fd < 0)
        return 1;
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    fprintf(stderr, "đã kết nối %s:%s. Gõ JSON, /ping hoặc /quit\n", host, port);

    frame_decoder_t dec;
    frame_decoder_init(&dec);
    char *line = NULL;
    size_t cap = 0;
    unsigned next_id = 1;
    int rc = 0;

    struct pollfd pfd[2] = {
        { .fd = STDIN_FILENO, .events = POLLIN },
        { .fd = fd,           .events = POLLIN },
    };

    int stdin_done = 0;                  /* stdin hết (Ctrl+D / pipe): chờ nốt phản hồi */
    for (;;) {
        int pr = poll(pfd, 2, stdin_done ? 500 : -1);
        if (pr < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            rc = 1;
            break;
        }
        if (pr == 0)
            break;                       /* stdin đã hết và 0,5s không có gì thêm */

        if (pfd[1].revents & (POLLIN | POLLHUP | POLLERR)) {
            uint8_t buf[16384];
            ssize_t n = recv(fd, buf, sizeof buf, 0);
            if (n == 0) {
                fprintf(stderr, "server đã đóng kết nối\n");
                break;
            }
            if (n < 0) {
                if (errno == EINTR)
                    continue;
                fprintf(stderr, "recv lỗi: %s\n", strerror(errno));
                rc = 1;
                break;
            }
            if (frame_decoder_feed(&dec, buf, (size_t)n) < 0) {
                fprintf(stderr, "hết bộ nhớ\n");
                rc = 1;
                break;
            }
            for (;;) {
                const char *p;
                uint32_t len;
                frame_status_t st = frame_decoder_next(&dec, &p, &len);
                if (st == FRAME_NEED_MORE)
                    break;
                if (st == FRAME_ERROR) {
                    fprintf(stderr, "server gửi sai khung dữ liệu\n");
                    rc = 1;
                    goto out;
                }
                printf("<< %s\n", p);
                fflush(stdout);
            }
        }

        if (pfd[0].revents & (POLLIN | POLLHUP)) {
            ssize_t len = getline(&line, &cap, stdin);
            if (len < 0) {                /* Ctrl+D hoặc hết dữ liệu pipe */
                stdin_done = 1;
                pfd[0].fd = -1;          /* poll bỏ qua fd âm */
                continue;
            }
            while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
                line[--len] = '\0';
            if (len == 0)
                continue;

            if (strcmp(line, "/quit") == 0)
                break;
            if (strcmp(line, "/ping") == 0) {
                char msg[64];
                int m = snprintf(msg, sizeof msg,
                                 "{\"type\":\"PING\",\"id\":\"c-%u\"}", next_id++);
                if (send_payload(fd, msg, (size_t)m) < 0) {
                    fprintf(stderr, "gửi lỗi: %s\n", strerror(errno));
                    rc = 1;
                    break;
                }
                continue;
            }
            if (send_payload(fd, line, (size_t)len) < 0) {
                fprintf(stderr, "gửi lỗi: %s\n", strerror(errno));
                rc = 1;
                break;
            }
        }
    }

out:
    free(line);
    frame_decoder_free(&dec);
    close(fd);
    return rc;
}
