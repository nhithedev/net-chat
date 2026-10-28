/*
 * server.c - điểm vào của server. Nối net với msg và auth (PING/PONG,
 * REGISTER, LOGIN); handler nghiệp vụ khác sẽ đăng ký thêm ở đây.
 *
 * Dùng: netchat-server [port]     (mặc định 9000)
 * DB:   biến môi trường NETCHAT_DB = conninfo libpq (mặc định chat@127.0.0.1:5432)
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "auth.h"
#include "msg.h"
#include "net.h"
#include "store.h"

static net_server_t *g_server;

static void on_signal(int sig)
{
    (void)sig;
    if (g_server)
        net_server_stop(g_server);
}

int main(int argc, char **argv)
{
    uint16_t port = 0;                   /* 0 = mặc định 9000 */
    if (argc > 1) {
        long p = strtol(argv[1], NULL, 10);
        if (p <= 0 || p > 65535) {
            fprintf(stderr, "Dùng: %s [port]\n", argv[0]);
            return 2;
        }
        port = (uint16_t)p;
    }

    const char *dsn = getenv("NETCHAT_DB");
    if (!dsn || !*dsn)
        dsn = "host=127.0.0.1 port=5432 dbname=chat user=chat password=chat";
    if (!store_init(dsn))
        return 1;

    net_callbacks_t cb = {
        .on_payload = msg_dispatch,      /* mọi payload đi qua msg */
        .on_close   = auth_net_on_close,
        .is_authed  = auth_net_is_authed,
    };
    auth_init();

    g_server = net_server_create(port, &cb);
    if (!g_server) {
        store_shutdown();
        return 1;
    }

    struct sigaction sa = { .sa_handler = on_signal };   /* không SA_RESTART */
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    int rc = net_server_run(g_server);
    net_log(NULL, "đang tắt server");
    net_server_destroy(g_server);
    store_shutdown();
    return rc == 0 ? 0 : 1;
}
