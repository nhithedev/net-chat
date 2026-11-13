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
    PGresult *res = run("SELECT username, display_name, password_hash, salt, id "
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
        u->id = strtoll(PQgetvalue(res, 0, 4), NULL, 10);
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

/* ================================================================
 * Bạn bè
 * ================================================================ */

/* Lệnh ghi: trả OK nếu đúng 1 dòng bị ảnh hưởng, NOT_FOUND nếu 0 dòng. */
static store_status_t run_one_row(const char *sql, int n, const char *const *params)
{
    PGresult *res = run(sql, n, params, PGRES_COMMAND_OK);
    if (!res)
        return STORE_ERROR;
    store_status_t st;
    if (PQresultStatus(res) != PGRES_COMMAND_OK)
        st = STORE_ERROR;
    else
        st = strcmp(PQcmdTuples(res), "0") == 0 ? STORE_NOT_FOUND : STORE_OK;
    PQclear(res);
    return st;
}

static bool user_exists(const char *username)
{
    const char *p[1] = { username };
    PGresult *res = run("SELECT 1 FROM users WHERE username = $1",
                        1, p, PGRES_TUPLES_OK);
    bool ok = res && PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) == 1;
    PQclear(res);
    return ok;
}

bool store_are_friends(const char *a, const char *b)
{
    const char *p[2] = { a, b };
    PGresult *res = run(
        "SELECT 1 FROM users a, users b, friendships f "
        "WHERE a.username = $1 AND b.username = $2 "
        "AND f.user_low = LEAST(a.id, b.id) AND f.user_high = GREATEST(a.id, b.id)",
        2, p, PGRES_TUPLES_OK);
    bool ok = res && PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) == 1;
    PQclear(res);
    return ok;
}

store_status_t store_friend_request(const char *from, const char *to)
{
    if (!user_exists(to))
        return STORE_NOT_FOUND;
    if (store_are_friends(from, to))
        return STORE_EXISTS;

    const char *p[2] = { from, to };
    PGresult *res = run(
        "INSERT INTO friend_requests (requester_id, addressee_id) "
        "SELECT a.id, b.id FROM users a, users b "
        "WHERE a.username = $1 AND b.username = $2",
        2, p, PGRES_COMMAND_OK);
    if (!res)
        return STORE_ERROR;
    store_status_t st = STORE_OK;
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        /* chỉ mục duy nhất theo cặp: đã có lời mời chờ (kể cả chiều ngược lại) */
        const char *state = PQresultErrorField(res, PG_DIAG_SQLSTATE);
        st = (state && strcmp(state, "23505") == 0) ? STORE_EXISTS : STORE_ERROR;
    }
    PQclear(res);
    return st;
}

store_status_t store_friend_accept(const char *me, const char *from)
{
    const char *p[2] = { me, from };
    /* Một câu lệnh: đổi trạng thái lời mời rồi tạo quan hệ bạn bè (nguyên tử). */
    return run_one_row(
        "WITH r AS ("
        "  UPDATE friend_requests SET status = 'accepted', responded_at = now() "
        "  WHERE status = 'pending' "
        "    AND addressee_id = (SELECT id FROM users WHERE username = $1) "
        "    AND requester_id = (SELECT id FROM users WHERE username = $2) "
        "  RETURNING requester_id, addressee_id) "
        "INSERT INTO friendships (user_low, user_high) "
        "SELECT LEAST(requester_id, addressee_id), GREATEST(requester_id, addressee_id) "
        "FROM r",
        2, p);
}

store_status_t store_friend_reject(const char *me, const char *from)
{
    const char *p[2] = { me, from };
    return run_one_row(
        "UPDATE friend_requests SET status = 'rejected', responded_at = now() "
        "WHERE status = 'pending' "
        "  AND addressee_id = (SELECT id FROM users WHERE username = $1) "
        "  AND requester_id = (SELECT id FROM users WHERE username = $2)",
        2, p);
}

store_status_t store_friend_remove(const char *me, const char *other)
{
    const char *p[2] = { me, other };
    return run_one_row(
        "DELETE FROM friendships f USING users a, users b "
        "WHERE a.username = $1 AND b.username = $2 "
        "AND f.user_low = LEAST(a.id, b.id) AND f.user_high = GREATEST(a.id, b.id)",
        2, p);
}

static void each_user(const char *sql, const char *me, store_user_cb cb, void *ud)
{
    const char *p[1] = { me };
    PGresult *res = run(sql, 1, p, PGRES_TUPLES_OK);
    if (!res)
        return;
    if (PQresultStatus(res) == PGRES_TUPLES_OK)
        for (int i = 0; i < PQntuples(res); i++)
            cb(PQgetvalue(res, i, 0), PQgetvalue(res, i, 1), ud);
    PQclear(res);
}

void store_friends_each(const char *me, store_user_cb cb, void *ud)
{
    each_user("SELECT o.username, o.display_name "
              "FROM users m JOIN friendships f ON m.id IN (f.user_low, f.user_high) "
              "JOIN users o ON o.id = f.user_low + f.user_high - m.id "
              "WHERE m.username = $1 ORDER BY o.username",
              me, cb, ud);
}

void store_pending_in_each(const char *me, store_user_cb cb, void *ud)
{
    each_user("SELECT u.username, u.display_name "
              "FROM friend_requests r JOIN users u ON u.id = r.requester_id "
              "JOIN users m ON m.id = r.addressee_id "
              "WHERE m.username = $1 AND r.status = 'pending' ORDER BY u.username",
              me, cb, ud);
}

void store_pending_out_each(const char *me, store_user_cb cb, void *ud)
{
    each_user("SELECT u.username, u.display_name "
              "FROM friend_requests r JOIN users u ON u.id = r.addressee_id "
              "JOIN users m ON m.id = r.requester_id "
              "WHERE m.username = $1 AND r.status = 'pending' ORDER BY u.username",
              me, cb, ud);
}

void store_online_each(store_user_cb cb, void *ud)
{
    for (size_t i = 0; i < g_users_n; i++)
        if (g_users[i]->conn)
            cb(g_users[i]->username, g_users[i]->display_name, ud);
}

/* ================================================================
 * Tin nhắn 1-1
 * ================================================================ */

bool store_msg_save(const char *from, const char *to, const char *text,
                    bool delivered, long long *msg_id, long long *ts_ms)
{
    const char *p2[2] = { from, to };
    PGresult *res = run(
        "INSERT INTO conversations (type, direct_key) "
        "SELECT 'direct', LEAST(a.id, b.id) || ':' || GREATEST(a.id, b.id) "
        "FROM users a, users b WHERE a.username = $1 AND b.username = $2 "
        "ON CONFLICT (direct_key) DO NOTHING",
        2, p2, PGRES_COMMAND_OK);
    if (!res || PQresultStatus(res) != PGRES_COMMAND_OK) {
        PQclear(res);
        return false;
    }
    PQclear(res);

    const char *p3[3] = { from, to, text };
    res = run(
        "INSERT INTO messages (conversation_id, sender_id, text) "
        "SELECT c.id, a.id, $3 FROM users a, users b, conversations c "
        "WHERE a.username = $1 AND b.username = $2 "
        "AND c.direct_key = LEAST(a.id, b.id) || ':' || GREATEST(a.id, b.id) "
        "RETURNING id, (extract(epoch FROM created_at) * 1000)::bigint",
        3, p3, PGRES_TUPLES_OK);
    if (!res || PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) != 1) {
        PQclear(res);
        return false;
    }
    char idbuf[32];
    snprintf(idbuf, sizeof idbuf, "%s", PQgetvalue(res, 0, 0));
    *msg_id = strtoll(idbuf, NULL, 10);
    *ts_ms = strtoll(PQgetvalue(res, 0, 1), NULL, 10);
    PQclear(res);

    /* Biên nhận cho người nhận: delivered_at NULL = tin offline chưa giao. */
    const char *pr[3] = { idbuf, to, delivered ? "t" : "f" };
    res = run("INSERT INTO message_receipts (message_id, user_id, delivered_at) "
              "SELECT $1::bigint, id, CASE WHEN $3::boolean THEN now() END "
              "FROM users WHERE username = $2",
              3, pr, PGRES_COMMAND_OK);
    PQclear(res);
    return true;
}

void store_msg_history(const char *a, const char *b, int limit,
                       store_msg_cb cb, void *ud)
{
    char lim[16];
    snprintf(lim, sizeof lim, "%d", limit);
    const char *p[3] = { a, b, lim };
    PGresult *res = run(
        "SELECT * FROM ("
        "  SELECT m.id, s.username AS sender, m.text, "
        "         (extract(epoch FROM m.created_at) * 1000)::bigint AS ts "
        "  FROM messages m "
        "  JOIN conversations c ON c.id = m.conversation_id "
        "  JOIN users ua ON ua.username = $1 "
        "  JOIN users ub ON ub.username = $2 "
        "  JOIN users s ON s.id = m.sender_id "
        "  WHERE c.direct_key = LEAST(ua.id, ub.id) || ':' || GREATEST(ua.id, ub.id) "
        "    AND m.deleted_at IS NULL "
        "  ORDER BY m.id DESC LIMIT $3::int) t ORDER BY id",
        3, p, PGRES_TUPLES_OK);
    if (!res)
        return;
    if (PQresultStatus(res) == PGRES_TUPLES_OK)
        for (int i = 0; i < PQntuples(res); i++)
            cb(strtoll(PQgetvalue(res, i, 0), NULL, 10), PQgetvalue(res, i, 1),
               PQgetvalue(res, i, 2), strtoll(PQgetvalue(res, i, 3), NULL, 10), ud);
    PQclear(res);
}

bool store_token_create(const store_user_t *u, char *token_out)
{
    char token[STORE_TOKEN_HEX_LEN + 1];
    if (!random_hex(STORE_TOKEN_HEX_LEN / 2, token))
        return false;

    /* Dọn token đã hết hạn cho bảng khỏi phình; lỗi ở đây không ảnh hưởng đăng nhập. */
    PQclear(run("DELETE FROM sessions WHERE expires_at < now()", 0, NULL,
                PGRES_COMMAND_OK));

    /* Hạn 1 ngày khi đang kết nối; store_token_hold rút còn 60s lúc mất kết nối. */
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

store_user_t *store_token_resume(const char *token)
{
    const char *p[1] = { token };
    PGresult *res = run("UPDATE sessions s SET expires_at = now() + interval '1 day' "
                        "FROM users u WHERE s.token = $1 AND s.expires_at > now() "
                        "AND u.id = s.user_id RETURNING u.username",
                        1, p, PGRES_TUPLES_OK);
    store_user_t *u = NULL;
    if (res && PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) == 1)
        u = store_user_find(PQgetvalue(res, 0, 0));
    PQclear(res);
    return u;
}

void store_token_hold(const store_user_t *u)
{
    char secs[16];
    snprintf(secs, sizeof secs, "%d", STORE_RESUME_HOLD_S);
    const char *p[2] = { u->username, secs };
    PGresult *res = run("UPDATE sessions SET expires_at = "
                        "LEAST(expires_at, now() + $2::int * interval '1 second') "
                        "WHERE user_id = (SELECT id FROM users WHERE username = $1)",
                        2, p, PGRES_COMMAND_OK);
    PQclear(res);
}

void store_token_revoke(const store_user_t *u)
{
    const char *p[1] = { u->username };
    PQclear(run("DELETE FROM sessions WHERE user_id = "
                "(SELECT id FROM users WHERE username = $1)",
                1, p, PGRES_COMMAND_OK));
}
