#include "pc_auth.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "pc_auth";

#define NVS_NS         "pc_ctl"
#define NVS_KEY_AUTH   "auth"
#define NVS_KEY_TOKEN  "api_token"
#define AUTH_VERSION   1
#define SALT_LEN       16
#define HASH_LEN       32
#define MAX_SESSIONS   4
#define SESSION_ID_LEN 32        /* hex chars */

#define DEFAULT_USER "admin"
#define DEFAULT_PASS "admin"

typedef struct __attribute__((packed)) {
    uint8_t version;
    char    username[PC_AUTH_USER_MAX + 1];
    uint8_t salt[SALT_LEN];
    uint8_t hash[HASH_LEN];
} pc_auth_blob_t;

typedef struct {
    char    id[SESSION_ID_LEN + 1];
    int64_t expires_us;
} pc_auth_session_t;

static SemaphoreHandle_t s_lock;
static nvs_handle_t s_nvs;
static bool s_nvs_ok;
static bool s_creds_loaded;                  /* false => default admin/admin */
static pc_auth_blob_t s_creds;
static char s_token[PC_AUTH_TOKEN_MAX + 1];  /* runtime token ("" = none) */
static pc_auth_session_t s_sessions[MAX_SESSIONS];

static void lock(void) { if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { if (s_lock) xSemaphoreGive(s_lock); }

/* ---------- SHA-256 (self-contained) ----------
 * ESP-IDF 6.x ships mbedTLS 4.x, which no longer exposes mbedtls/sha256.h.
 * A compact, dependency-free SHA-256 keeps the credential hashing stable
 * across IDF/mbedTLS versions. */

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  buf[64];
    size_t   buflen;
} sha256_ctx_t;

static const uint32_t SHA256_K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static uint32_t rotr32(uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
}

static void sha256_compress(sha256_ctx_t *c, const uint8_t *p) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) |
               ((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = c->state[0], b = c->state[1], cc = c->state[2], d = c->state[3];
    uint32_t e = c->state[4], f = c->state[5], g = c->state[6], h = c->state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + SHA256_K[i] + w[i];
        uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & cc) ^ (b & cc);
        uint32_t t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->state[0] += a; c->state[1] += b; c->state[2] += cc; c->state[3] += d;
    c->state[4] += e; c->state[5] += f; c->state[6] += g; c->state[7] += h;
}

static void sha256_init(sha256_ctx_t *c) {
    static const uint32_t init[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    };
    memcpy(c->state, init, sizeof(init));
    c->bitlen = 0;
    c->buflen = 0;
}

static void sha256_update(sha256_ctx_t *c, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        c->buf[c->buflen++] = data[i];
        if (c->buflen == 64) {
            sha256_compress(c, c->buf);
            c->bitlen += 512;
            c->buflen = 0;
        }
    }
}

static void sha256_final(sha256_ctx_t *c, uint8_t out[32]) {
    size_t i = c->buflen;
    c->bitlen += (uint64_t)c->buflen * 8;
    c->buf[i++] = 0x80;
    if (i > 56) {
        while (i < 64) c->buf[i++] = 0;
        sha256_compress(c, c->buf);
        i = 0;
    }
    while (i < 56) c->buf[i++] = 0;
    for (int k = 7; k >= 0; k--) c->buf[i++] = (uint8_t)(c->bitlen >> (k * 8));
    sha256_compress(c, c->buf);
    for (int k = 0; k < 8; k++) {
        out[k * 4]     = (uint8_t)(c->state[k] >> 24);
        out[k * 4 + 1] = (uint8_t)(c->state[k] >> 16);
        out[k * 4 + 2] = (uint8_t)(c->state[k] >> 8);
        out[k * 4 + 3] = (uint8_t)(c->state[k]);
    }
}

/* ---------- password hashing ---------- */

static void hash_password(const uint8_t salt[SALT_LEN], const char *password,
                          uint8_t out[HASH_LEN]) {
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, salt, SALT_LEN);
    sha256_update(&c, (const uint8_t *)password, strlen(password));
    sha256_final(&c, out);
    memset(&c, 0, sizeof(c));
}

static bool ct_equal(const uint8_t *a, const uint8_t *b, size_t n) {
    unsigned diff = 0;
    for (size_t i = 0; i < n; i++) diff |= (unsigned)(a[i] ^ b[i]);
    return diff == 0;
}

static bool username_valid(const char *u) {
    size_t n = u ? strlen(u) : 0;
    if (n == 0 || n > PC_AUTH_USER_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)u[i];
        if (c < 0x21 || c > 0x7e) return false;   /* printable, no spaces */
    }
    return true;
}

static bool password_valid(const char *p) {
    size_t n = p ? strlen(p) : 0;
    if (n < PC_AUTH_PASS_MIN || n > PC_AUTH_PASS_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)p[i];
        if (c < 0x20 || c > 0x7e) return false;   /* printable ASCII */
    }
    return true;
}

static bool token_valid(const char *t) {
    size_t n = t ? strlen(t) : 0;
    if (n == 0) return true;                      /* empty = clear */
    if (n < PC_AUTH_TOKEN_MIN || n > PC_AUTH_TOKEN_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)t[i];
        if (c < 0x21 || c > 0x7e) return false;   /* no spaces/control/UTF-8 */
    }
    return true;
}

/* ---------- init ---------- */

void pc_auth_init(void) {
    if (!s_lock) s_lock = xSemaphoreCreateMutex();

    /* Default admin/admin lives in RAM only; it is persisted the first time
     * the user changes something (the usual "no NVS write without a user
     * action" rule). */
    memset(&s_creds, 0, sizeof(s_creds));
    s_creds.version = AUTH_VERSION;
    snprintf(s_creds.username, sizeof(s_creds.username), "%s", DEFAULT_USER);
    s_creds_loaded = false;

    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS namespace '%s' open failed (%s) - defaults apply",
                 NVS_NS, esp_err_to_name(err));
        return;
    }
    s_nvs_ok = true;

    pc_auth_blob_t blob;
    size_t len = sizeof(blob);
    if (nvs_get_blob(s_nvs, NVS_KEY_AUTH, &blob, &len) == ESP_OK &&
        len == sizeof(blob) && blob.version == AUTH_VERSION &&
        blob.username[0] != '\0') {
        s_creds = blob;
        s_creds_loaded = true;
        ESP_LOGI(TAG, "admin account loaded from NVS (user=%s)", s_creds.username);
    } else {
        ESP_LOGI(TAG, "no stored admin account - default admin/admin active "
                      "(change it on /pc/security)");
    }

    char tok[PC_AUTH_TOKEN_MAX + 1];
    len = sizeof(tok);
    if (nvs_get_str(s_nvs, NVS_KEY_TOKEN, tok, &len) == ESP_OK && tok[0] != '\0') {
        snprintf(s_token, sizeof(s_token), "%s", tok);
        ESP_LOGI(TAG, "runtime API token loaded from NVS");
    }
    memset(tok, 0, sizeof(tok));
}

/* ---------- admin login ---------- */

bool pc_auth_verify(const char *username, const char *password) {
    if (!username || !password) return false;
    lock();
    bool user_ok = (strcmp(username, s_creds.username) == 0);
    bool pass_ok;
    if (!s_creds_loaded) {
        pass_ok = (strcmp(password, DEFAULT_PASS) == 0);
    } else {
        uint8_t h[HASH_LEN];
        hash_password(s_creds.salt, password, h);
        pass_ok = ct_equal(h, s_creds.hash, HASH_LEN);
        memset(h, 0, sizeof(h));
    }
    unlock();
    return user_ok && pass_ok;
}

esp_err_t pc_auth_change_credentials(const char *current_password,
                                     const char *new_username,
                                     const char *new_password) {
    if (!current_password) return ESP_ERR_INVALID_ARG;
    if (new_username && new_username[0] == '\0') new_username = NULL;
    if (new_password && new_password[0] == '\0') new_password = NULL;

    lock();
    bool was_default = !s_creds_loaded;
    bool cur_ok;
    if (was_default) {
        cur_ok = (strcmp(current_password, DEFAULT_PASS) == 0);
    } else {
        uint8_t h[HASH_LEN];
        hash_password(s_creds.salt, current_password, h);
        cur_ok = ct_equal(h, s_creds.hash, HASH_LEN);
        memset(h, 0, sizeof(h));
    }
    if (!cur_ok) {
        unlock();
        return ESP_ERR_INVALID_STATE;
    }

    pc_auth_blob_t blob = s_creds;
    blob.version = AUTH_VERSION;
    if (new_username) {
        if (!username_valid(new_username)) { unlock(); return ESP_ERR_INVALID_ARG; }
        snprintf(blob.username, sizeof(blob.username), "%s", new_username);
    }
    if (new_password) {
        if (!password_valid(new_password)) { unlock(); return ESP_ERR_INVALID_ARG; }
        esp_fill_random(blob.salt, SALT_LEN);
        hash_password(blob.salt, new_password, blob.hash);
    } else if (was_default) {
        /* username-only change while still on the default: persist a hash of
         * the default password so the stored blob stays consistent. */
        esp_fill_random(blob.salt, SALT_LEN);
        hash_password(blob.salt, DEFAULT_PASS, blob.hash);
    }
    unlock();

    if (!s_nvs_ok) return ESP_ERR_INVALID_STATE;
    esp_err_t err = nvs_set_blob(s_nvs, NVS_KEY_AUTH, &blob, sizeof(blob));
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err != ESP_OK) return err;

    lock();
    s_creds = blob;
    s_creds_loaded = true;
    unlock();
    ESP_LOGI(TAG, "admin credentials updated (user=%s)", blob.username);
    return ESP_OK;
}

void pc_auth_username(char *out, size_t out_len) {
    if (!out || out_len == 0) return;
    lock();
    snprintf(out, out_len, "%s", s_creds.username);
    unlock();
}


/* ---------- sessions (RAM only) ---------- */

bool pc_auth_session_create(char *out, size_t out_len) {
    if (!out || out_len < SESSION_ID_LEN + 1) return false;

    uint8_t raw[SESSION_ID_LEN / 2];
    esp_fill_random(raw, sizeof(raw));
    char id[SESSION_ID_LEN + 1];
    for (int i = 0; i < (int)sizeof(raw); i++) {
        snprintf(id + i * 2, 3, "%02x", raw[i]);
    }
    id[SESSION_ID_LEN] = '\0';

    int64_t now = esp_timer_get_time();
    lock();
    int slot = 0;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (s_sessions[i].expires_us <= now) { slot = i; break; }  /* free/expired */
    }
    snprintf(s_sessions[slot].id, sizeof(s_sessions[slot].id), "%s", id);
    s_sessions[slot].expires_us = now + (int64_t)PC_AUTH_SESSION_TTL_S * 1000000;
    unlock();

    snprintf(out, out_len, "%s", id);
    return true;
}

bool pc_auth_session_valid(const char *id) {
    if (!id || id[0] == '\0') return false;
    int64_t now = esp_timer_get_time();
    bool ok = false;
    lock();
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (s_sessions[i].expires_us > now &&
            strcmp(s_sessions[i].id, id) == 0) {
            /* sliding expiry */
            s_sessions[i].expires_us = now + (int64_t)PC_AUTH_SESSION_TTL_S * 1000000;
            ok = true;
            break;
        }
    }
    unlock();
    return ok;
}

void pc_auth_session_destroy(const char *id) {
    if (!id || id[0] == '\0') return;
    lock();
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (strcmp(s_sessions[i].id, id) == 0) {
            s_sessions[i].id[0] = '\0';
            s_sessions[i].expires_us = 0;
        }
    }
    unlock();
}


/* ---------- runtime API token ---------- */

bool pc_auth_api_token(char *out, size_t out_len) {
    if (!out || out_len == 0) return false;
    out[0] = '\0';
    lock();
    bool set = (s_token[0] != '\0');
    if (set) snprintf(out, out_len, "%s", s_token);
    unlock();
    if (set) return true;

    const char *fallback = CONFIG_PCCTL_API_TOKEN;
    if (fallback && fallback[0] != '\0') {
        snprintf(out, out_len, "%s", fallback);
        return true;
    }
    return false;
}

bool pc_auth_api_token_overridden(void) {
    lock();
    bool o = (s_token[0] != '\0');
    unlock();
    return o;
}

esp_err_t pc_auth_set_api_token(const char *token) {
    if (!token) token = "";
    if (!token_valid(token)) return ESP_ERR_INVALID_ARG;
    if (!s_nvs_ok) return ESP_ERR_INVALID_STATE;

    esp_err_t err;
    if (token[0] == '\0') {
        /* clear the runtime override (falls back to the Kconfig token) */
        err = nvs_erase_key(s_nvs, NVS_KEY_TOKEN);
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
        if (err == ESP_OK) err = nvs_commit(s_nvs);
        if (err != ESP_OK) return err;
        lock();
        s_token[0] = '\0';
        unlock();
        ESP_LOGI(TAG, "runtime API token cleared");
        return ESP_OK;
    }

    err = nvs_set_str(s_nvs, NVS_KEY_TOKEN, token);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err != ESP_OK) return err;
    lock();
    snprintf(s_token, sizeof(s_token), "%s", token);
    unlock();
    ESP_LOGI(TAG, "runtime API token updated");
    return ESP_OK;
}

esp_err_t pc_auth_generate_api_token(char *out, size_t out_len) {
    if (!out || out_len < PC_AUTH_TOKEN_MAX + 1) return ESP_ERR_INVALID_ARG;
    uint8_t raw[24];                        /* 24 bytes -> 48 hex chars */
    esp_fill_random(raw, sizeof(raw));
    char tok[PC_AUTH_TOKEN_MAX + 1];
    for (int i = 0; i < (int)sizeof(raw); i++) {
        snprintf(tok + i * 2, 3, "%02x", raw[i]);
    }
    tok[sizeof(raw) * 2] = '\0';

    esp_err_t err = pc_auth_set_api_token(tok);
    if (err != ESP_OK) return err;
    snprintf(out, out_len, "%s", tok);
    return ESP_OK;
}

