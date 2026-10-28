/*
 * store.c - lưu user/token vào PostgreSQL bằng libpq (xem store.h).
 *
 * Mọi câu lệnh dùng PQexecParams với tham số $1.. (không nối chuỗi SQL).
 * PQexecParams chặn thread; chấp nhận được vì server một thread và DB cùng máy.
 */
#include "store.h"

#include <openssl/crypto.h>
#include <libpq-fe.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>

static PGconn         *g_db;
/* Cache user đã tải: mảng con trỏ nên địa chỉ user không đổi khi realloc. */
static store_user_t  **g_users;
static size_t          g_users_n, g_users_cap;

static void to_hex(const unsigned char *in, size_t n, char *out)
{
    static const char H[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i]     = H[in[i] >> 4];
        out[2 * i + 1] = H[in[i] & 0x0F];
    }
    out[2 * n] = '\0';
}

static bool random_hex(size_t nbytes, char *out)
{
    unsigned char buf[32];
    if (nbytes > sizeof buf)
        return false;
    size_t got = 0;
    while (got < nbytes) {
        ssize_t r = getrandom(buf + got, nbytes - got, 0);
        if (r <= 0)
            return false;
        got += (size_t)r;
    }
    to_hex(buf, nbytes, out);
    return true;
}

/* hash = hex(SHA-256(salt_hex || password)) */
static bool hash_password(const char *salt, const char *password, char *out)
{
    size_t sl = strlen(salt), pl = strlen(password);
    unsigned char *in = malloc(sl + pl);
    if (!in)
        return false;
    memcpy(in, salt, sl);
    memcpy(in + sl, password, pl);
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(in, sl + pl, digest);
    OPENSSL_cleanse(in, sl + pl);
    free(in);
    to_hex(digest, sizeof digest, out);
    return true;
}

static store_user_t *cache_find(const char *username)
{
    for (size_t i = 0; i < g_users_n; i++)
        if (strcmp(g_users[i]->username, username) == 0)
            return g_users[i];
    return NULL;
}

/* Thêm *u (đã malloc) vào cache; nhận sở hữu nếu thành công. */
static bool cache_add(store_user_t *u)
{
    if (g_users_n == g_users_cap) {
        size_t cap = g_users_cap ? g_users_cap * 2 : 16;
        store_user_t **p = realloc(g_users, cap * sizeof *p);
        if (!p)
            return false;
        g_users = p;
        g_users_cap = cap;
    }
    g_users[g_users_n++] = u;
    return true;
}

bool store_init(const char *conninfo)
{
    g_db = PQconnectdb(conninfo);
    if (PQstatus(g_db) != CONNECTION_OK) {
        fprintf(stderr, "không kết nối được PostgreSQL: %s", PQerrorMessage(g_db));
        PQfinish(g_db);
        g_db = NULL;
        return false;
    }
    return true;
}

void store_shutdown(void)
{
    for (size_t i = 0; i < g_users_n; i++)
        free(g_users[i]);
    free(g_users);
    g_users = NULL;
    g_users_n = g_users_cap = 0;
    if (g_db) {
        PQfinish(g_db);
        g_db = NULL;
    }
}

/* Chạy 1 lệnh có tham số; trả result (caller PQclear) hoặc NULL nếu lỗi nặng. */
static PGresult *run(const char *sql, int n, const char *const *params,
                     ExecStatusType expect)
{
    if (!g_db)
        return NULL;
    PGresult *res = PQexecParams(g_db, sql, n, NULL, params, NULL, NULL, 0);
    if (PQresultStatus(res) != expect) {
        /* 23505 (trùng khóa) do caller tự xử lý nên không coi là lỗi ở đây. */
        const char *state = PQresultErrorField(res, PG_DIAG_SQLSTATE);
        if (!state || strcmp(state, "23505") != 0)
            fprintf(stderr, "lỗi SQL: %s", PQerrorMessage(g_db));
        return res;                     /* trả luôn để caller đọc SQLSTATE */
    }
    return res;
}

store_user_t *store_user_find(const char *username)
{
    store_user_t *u = cache_find(username);
    if (u)
        return u;

    const char *params[1] = { username };
    PGresult *res = run("SELECT username, display_name, password_hash, salt "
                        "FROM users WHERE username = $1",
                        1, params, PGRES_TUPLES_OK);
    if (!res)
        return NULL;
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) != 1) {
        PQclear(res);
        return NULL;
    }

    u = calloc(1, sizeof *u);
    if (u) {
        snprintf(u->username, sizeof u->username, "%s", PQgetvalue(res, 0, 0));
        snprintf(u->display_name, sizeof u->display_name, "%s", PQgetvalue(res, 0, 1));
        snprintf(u->hash, sizeof u->hash, "%s", PQgetvalue(res, 0, 2));
        snprintf(u->salt, sizeof u->salt, "%s", PQgetvalue(res, 0, 3));
        if (!cache_add(u)) {
            free(u);
            u = NULL;
        }
    }
    PQclear(res);
    return u;
}

store_status_t store_user_create(const char *username, const char *display_name,
                                 const char *password)
{
    if (strlen(username) > 20 || strlen(display_name) > 120)
        return STORE_ERROR;

    char salt[STORE_SALT_HEX_LEN + 1], hash[STORE_HASH_HEX_LEN + 1];
    if (!random_hex(STORE_SALT_HEX_LEN / 2, salt) ||
        !hash_password(salt, password, hash))
        return STORE_ERROR;

    const char *params[4] = { username, display_name, hash, salt };
    PGresult *res = run("INSERT INTO users (username, display_name, password_hash, salt) "
                        "VALUES ($1, $2, $3, $4)",
                        4, params, PGRES_COMMAND_OK);
    if (!res)
        return STORE_ERROR;
    store_status_t st = STORE_OK;
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        const char *state = PQresultErrorField(res, PG_DIAG_SQLSTATE);
        st = (state && strcmp(state, "23505") == 0) ? STORE_EXISTS : STORE_ERROR;
    }
    PQclear(res);
    return st;          /* cache được nạp ở lần store_user_find đầu tiên */
}

bool store_user_check_password(const store_user_t *u, const char *password)
{
    char h[STORE_HASH_HEX_LEN + 1];
    if (!hash_password(u->salt, password, h))
        return false;
    return CRYPTO_memcmp(h, u->hash, STORE_HASH_HEX_LEN) == 0;
}

bool store_token_create(const store_user_t *u, char *token_out)
{
    char token[STORE_TOKEN_HEX_LEN + 1];
    if (!random_hex(STORE_TOKEN_HEX_LEN / 2, token))
        return false;

    /* Hạn tạm 1 ngày; khi làm RESUME sẽ đặt now()+60s lúc mất kết nối. */
    const char *params[2] = { token, u->username };
    PGresult *res = run("INSERT INTO sessions (token, user_id, expires_at) "
                        "SELECT $1, id, now() + interval '1 day' "
                        "FROM users WHERE username = $2",
                        2, params, PGRES_COMMAND_OK);
    bool ok = res && PQresultStatus(res) == PGRES_COMMAND_OK &&
              strcmp(PQcmdTuples(res), "1") == 0;
    PQclear(res);
    if (ok)
        memcpy(token_out, token, sizeof token);
    return ok;
}
