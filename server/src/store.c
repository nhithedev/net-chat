/*
 * store.c - mock lưu trữ trong RAM (xem store.h). Tìm tuyến tính: đủ cho
 * thử nghiệm; bản PostgreSQL sẽ thay toàn bộ file này.
 */
#include "store.h"

#include <openssl/crypto.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>

typedef struct {
    char          token[STORE_TOKEN_HEX_LEN + 1];
    store_user_t *user;
} token_entry_t;

static store_user_t  **g_users;         /* mảng con trỏ: địa chỉ user không đổi khi realloc */
static size_t          g_users_n, g_users_cap;
static token_entry_t  *g_tokens;
static size_t          g_tokens_n, g_tokens_cap;

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

store_user_t *store_user_find(const char *username)
{
    for (size_t i = 0; i < g_users_n; i++)
        if (strcmp(g_users[i]->username, username) == 0)
            return g_users[i];
    return NULL;
}

store_status_t store_user_create(const char *username, const char *display_name,
                                 const char *password)
{
    if (store_user_find(username))
        return STORE_EXISTS;
    if (strlen(username) > 20 || strlen(display_name) > 120)
        return STORE_ERROR;

    if (g_users_n == g_users_cap) {
        size_t cap = g_users_cap ? g_users_cap * 2 : 16;
        store_user_t **p = realloc(g_users, cap * sizeof *p);
        if (!p)
            return STORE_ERROR;
        g_users = p;
        g_users_cap = cap;
    }

    store_user_t *u = calloc(1, sizeof *u);
    if (!u)
        return STORE_ERROR;
    snprintf(u->username, sizeof u->username, "%s", username);
    snprintf(u->display_name, sizeof u->display_name, "%s", display_name);
    if (!random_hex(STORE_SALT_HEX_LEN / 2, u->salt) ||
        !hash_password(u->salt, password, u->hash)) {
        free(u);
        return STORE_ERROR;
    }
    g_users[g_users_n++] = u;
    return STORE_OK;
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
    if (g_tokens_n == g_tokens_cap) {
        size_t cap = g_tokens_cap ? g_tokens_cap * 2 : 16;
        token_entry_t *p = realloc(g_tokens, cap * sizeof *p);
        if (!p)
            return false;
        g_tokens = p;
        g_tokens_cap = cap;
    }
    token_entry_t *e = &g_tokens[g_tokens_n];
    if (!random_hex(STORE_TOKEN_HEX_LEN / 2, e->token))
        return false;
    e->user = (store_user_t *)u;
    g_tokens_n++;
    memcpy(token_out, e->token, STORE_TOKEN_HEX_LEN + 1);
    return true;
}
