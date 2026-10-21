/* Buffers, strings, errors, response memory, base64 and SHA-256. */
#include "internal.h"

#include <stdlib.h>
#include <string.h>

const char *ollama_version(void) { return OLLAMA_C_VERSION; }

void ollama_free(void *ptr) { free(ptr); }

const char *ollama_status_string(ollama_status_t status) {
    switch (status) {
    case OLLAMA_OK: return "success";
    case OLLAMA_ERROR_REQUEST: return "invalid request";
    case OLLAMA_ERROR_RESPONSE: return "error response from the server";
    case OLLAMA_ERROR_CONNECTION: return "connection error";
    case OLLAMA_ERROR_TRANSPORT: return "transport error";
    case OLLAMA_ERROR_PARSE: return "invalid JSON response";
    case OLLAMA_ERROR_IO: return "I/O error";
    case OLLAMA_ERROR_MEMORY: return "out of memory";
    }
    return "unknown error";
}

/* ---- buffer ---- */

static bool buf_reserve(buf_t *b, size_t extra) {
    if (b->oom) return false;
    if (extra < b->cap - b->len) return true; /* keeps room for a NUL */
    size_t cap = b->cap ? b->cap : 256;
    while (cap - b->len <= extra) {
        if (cap > SIZE_MAX / 2) {
            b->oom = true;
            return false;
        }
        cap *= 2;
    }
    char *data = realloc(b->data, cap);
    if (!data) {
        b->oom = true;
        return false;
    }
    b->data = data;
    b->cap = cap;
    return true;
}

bool buf_append(buf_t *b, const void *data, size_t len) {
    if (!buf_reserve(b, len)) return false;
    if (len) memcpy(b->data + b->len, data, len);
    b->len += len;
    b->data[b->len] = '\0';
    return true;
}

bool buf_puts(buf_t *b, const char *s) { return buf_append(b, s, strlen(s)); }

bool buf_putc(buf_t *b, char c) { return buf_append(b, &c, 1); }

const char *buf_cstr(buf_t *b) {
    if (!b->data && !buf_reserve(b, 0)) return NULL;
    if (b->oom) return NULL;
    b->data[b->len] = '\0';
    return b->data;
}

void buf_free(buf_t *b) {
    free(b->data);
    memset(b, 0, sizeof(*b));
}

/* ---- strings ---- */

char *ol_strndup(const char *s, size_t n) {
    char *p = malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

char *ol_strdup(const char *s) { return s ? ol_strndup(s, strlen(s)) : NULL; }

void *ol_memdup(const void *p, size_t n) {
    void *q = malloc(n ? n : 1);
    if (q && n) memcpy(q, p, n);
    return q;
}

bool ascii_ieq(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return false;
    }
    return *a == *b;
}

char *ol_vformat(const char *fmt, va_list ap) {
    va_list copy;
    va_copy(copy, ap);
    int n = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (n < 0) return NULL;
    char *s = malloc((size_t)n + 1);
    if (s) vsnprintf(s, (size_t)n + 1, fmt, ap);
    return s;
}

char *ol_format(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *s = ol_vformat(fmt, ap);
    va_end(ap);
    return s;
}

bool read_file(const char *path, buf_t *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char chunk[64 * 1024];
    size_t n;
    bool ok = true;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        if (!buf_append(out, chunk, n)) {
            ok = false;
            break;
        }
    }
    if (ferror(f)) ok = false;
    fclose(f);
    return ok && buf_cstr(out) != NULL;
}

/* ---- errors ---- */

void err_init(err_t *e) {
    e->pub.code = OLLAMA_OK;
    e->pub.status_code = -1;
    e->pub.message = "";
    e->msg = NULL;
}

void err_free(err_t *e) {
    free(e->msg);
    e->msg = NULL;
}

void err_clear(err_t *e) {
    err_free(e);
    err_init(e);
}

ollama_status_t err_set(err_t *e, ollama_status_t code, int status_code, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *msg = ol_vformat(fmt, ap);
    va_end(ap);
    free(e->msg);
    e->msg = msg;
    e->pub.code = code;
    e->pub.status_code = status_code;
    e->pub.message = msg ? msg : ollama_status_string(code);
    return code;
}

ollama_status_t err_set_response(err_t *e, const char *body, size_t len, long status_code) {
    /* like ResponseError: use the "error" field of a JSON body, else the raw body */
    ollama_json_t *json = json_parse_n(body ? body : "", body ? len : 0);
    const char *error = ollama_json_as_string(ollama_json_get(json, "error"));
    ollama_status_t st;
    if (error)
        st = err_set(e, OLLAMA_ERROR_RESPONSE, (int)status_code, "%s", error);
    else if (len > 0)
        st = err_set(e, OLLAMA_ERROR_RESPONSE, (int)status_code, "%.*s", (int)len, body);
    else
        st = err_set(e, OLLAMA_ERROR_RESPONSE, (int)status_code, "HTTP %ld", status_code);
    ollama_json_free(json);
    return st;
}

/* ---- response memory ---- */

typedef union max_align {
    long double ld;
    double d;
    int64_t i;
    void *p;
    void (*f)(void);
} max_align_t_;

#define ALIGN_UP(n) (((n) + sizeof(max_align_t_) - 1) / sizeof(max_align_t_) * sizeof(max_align_t_))

typedef struct resp_chunk {
    struct resp_chunk *next;
} resp_chunk_t;

typedef struct resp_header {
    ollama_json_t *json;
    resp_chunk_t *chunks;
    bool failed;
} resp_header_t;

#define HEADER_SIZE ALIGN_UP(sizeof(resp_header_t))
#define CHUNK_SIZE ALIGN_UP(sizeof(resp_chunk_t))

static resp_header_t *header_of(const void *resp) { return (resp_header_t *)((char *)(uintptr_t)resp - HEADER_SIZE); }

void *resp_new(size_t size, ollama_json_t *json) {
    char *p = calloc(1, HEADER_SIZE + size);
    if (!p) {
        ollama_json_free(json);
        return NULL;
    }
    ((resp_header_t *)p)->json = json;
    return p + HEADER_SIZE;
}

void *resp_alloc(void *resp, size_t count, size_t size) {
    if (count == 0) return NULL;
    resp_header_t *h = header_of(resp);
    if (size && count > (SIZE_MAX - CHUNK_SIZE) / size) {
        h->failed = true;
        return NULL;
    }
    char *p = calloc(1, CHUNK_SIZE + count * size);
    if (!p) {
        h->failed = true;
        return NULL;
    }
    resp_chunk_t *chunk = (resp_chunk_t *)p;
    chunk->next = h->chunks;
    h->chunks = chunk;
    return p + CHUNK_SIZE;
}

bool resp_failed(const void *resp) { return header_of(resp)->failed; }

const ollama_json_t *resp_json(const void *resp) { return header_of(resp)->json; }

void ollama_response_free(void *response) {
    if (!response) return;
    resp_header_t *h = header_of(response);
    resp_chunk_t *chunk = h->chunks;
    while (chunk) {
        resp_chunk_t *next = chunk->next;
        free(chunk);
        chunk = next;
    }
    ollama_json_free(h->json);
    free(h);
}

/* ---- base64 ---- */

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char *ollama_base64_encode(const void *data, size_t size) {
    const unsigned char *in = data;
    if (size > (SIZE_MAX - 1) / 4 * 3 - 3) return NULL;
    char *out = malloc((size + 2) / 3 * 4 + 1);
    if (!out) return NULL;
    char *p = out;
    size_t i = 0;
    for (; i + 2 < size; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16 | (uint32_t)in[i + 1] << 8 | in[i + 2];
        *p++ = B64[v >> 18 & 63];
        *p++ = B64[v >> 12 & 63];
        *p++ = B64[v >> 6 & 63];
        *p++ = B64[v & 63];
    }
    if (i < size) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < size) v |= (uint32_t)in[i + 1] << 8;
        *p++ = B64[v >> 18 & 63];
        *p++ = B64[v >> 12 & 63];
        *p++ = i + 1 < size ? B64[v >> 6 & 63] : '=';
        *p++ = '=';
    }
    *p = '\0';
    return out;
}

static int b64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

unsigned char *ollama_base64_decode(const char *text, size_t *size) {
    if (size) *size = 0;
    if (!text) return NULL;
    size_t len = strlen(text);
    unsigned char *out = malloc(len / 4 * 3 + 4);
    if (!out) return NULL;
    size_t n = 0;
    uint32_t acc = 0;
    int bits = 0;
    bool padding = false;
    for (const char *p = text; *p; p++) {
        if (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') continue;
        if (*p == '=') {
            padding = true;
            continue;
        }
        int v = b64_value(*p);
        if (v < 0 || padding) {
            free(out);
            return NULL;
        }
        acc = acc << 6 | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[n++] = (unsigned char)(acc >> bits & 0xff);
        }
    }
    out[n] = '\0';
    if (size) *size = n;
    return out;
}

/* ---- SHA-256 (FIPS 180-4) ---- */

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define ROTR(x, n) ((x) >> (n) | (x) << (32 - (n)))

static void sha256_block(sha256_t *s, const unsigned char *p) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROTR(w[i - 15], 7) ^ ROTR(w[i - 15], 18) ^ w[i - 15] >> 3;
        uint32_t s1 = ROTR(w[i - 2], 17) ^ ROTR(w[i - 2], 19) ^ w[i - 2] >> 10;
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = s->state[0], b = s->state[1], c = s->state[2], d = s->state[3];
    uint32_t e = s->state[4], f = s->state[5], g = s->state[6], h = s->state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + (ROTR(e, 6) ^ ROTR(e, 11) ^ ROTR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        uint32_t t2 = (ROTR(a, 2) ^ ROTR(a, 13) ^ ROTR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    s->state[0] += a;
    s->state[1] += b;
    s->state[2] += c;
    s->state[3] += d;
    s->state[4] += e;
    s->state[5] += f;
    s->state[6] += g;
    s->state[7] += h;
}

void sha256_init(sha256_t *s) {
    static const uint32_t init[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(s->state, init, sizeof(init));
    s->bits = 0;
    s->used = 0;
}

void sha256_update(sha256_t *s, const void *data, size_t len) {
    const unsigned char *p = data;
    s->bits += (uint64_t)len * 8;
    while (len > 0) {
        size_t n = 64 - s->used;
        if (n > len) n = len;
        memcpy(s->block + s->used, p, n);
        s->used += n;
        p += n;
        len -= n;
        if (s->used == 64) {
            sha256_block(s, s->block);
            s->used = 0;
        }
    }
}

void sha256_final(sha256_t *s, unsigned char digest[32]) {
    uint64_t bits = s->bits;
    s->block[s->used++] = 0x80;
    if (s->used > 56) {
        memset(s->block + s->used, 0, 64 - s->used);
        sha256_block(s, s->block);
        s->used = 0;
    }
    memset(s->block + s->used, 0, 56 - s->used);
    for (int i = 0; i < 8; i++) s->block[56 + i] = (unsigned char)(bits >> (56 - 8 * i));
    sha256_block(s, s->block);
    for (int i = 0; i < 8; i++) {
        digest[4 * i] = (unsigned char)(s->state[i] >> 24);
        digest[4 * i + 1] = (unsigned char)(s->state[i] >> 16);
        digest[4 * i + 2] = (unsigned char)(s->state[i] >> 8);
        digest[4 * i + 3] = (unsigned char)s->state[i];
    }
}
