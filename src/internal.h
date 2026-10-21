/* Internal declarations shared by the library sources. Not installed. */
#ifndef OLLAMA_INTERNAL_H
#define OLLAMA_INTERNAL_H

#include "ollama.h"

/* curl.h includes the Windows headers: keep them small and free of macros
 * such as `small` (rpcndr.h), `min` and `max` */
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <curl/curl.h>
#include <stdarg.h>
#include <stdio.h>

/* ---- growable byte buffer (util.c) ---- */

typedef struct buf {
    char *data;
    size_t len;
    size_t cap;
    bool oom; /* set when an append failed */
} buf_t;

bool buf_append(buf_t *b, const void *data, size_t len);
bool buf_puts(buf_t *b, const char *s);
bool buf_putc(buf_t *b, char c);
/* Returns a NUL-terminated string (possibly empty), or NULL if the buffer failed. */
const char *buf_cstr(buf_t *b);
void buf_free(buf_t *b);

/* ---- strings (util.c) ---- */

char *ol_strdup(const char *s);
char *ol_strndup(const char *s, size_t n);
void *ol_memdup(const void *p, size_t n);
bool ascii_ieq(const char *a, const char *b);
char *ol_vformat(const char *fmt, va_list ap);
char *ol_format(const char *fmt, ...);
/* Reads a whole file. Returns false if it cannot be read. */
bool read_file(const char *path, buf_t *out);

/* ---- errors (util.c) ---- */

typedef struct err {
    ollama_error_t pub;
    char *msg;
} err_t;

void err_init(err_t *e);
void err_clear(err_t *e);
void err_free(err_t *e);
ollama_status_t err_set(err_t *e, ollama_status_t code, int status_code, const char *fmt, ...);
/* Sets OLLAMA_ERROR_RESPONSE from an HTTP error body ({"error": "..."} or raw text). */
ollama_status_t err_set_response(err_t *e, const char *body, size_t len, long status_code);

/* ---- sha256 (util.c) ---- */

typedef struct sha256 {
    uint32_t state[8];
    uint64_t bits;
    unsigned char block[64];
    size_t used;
} sha256_t;

void sha256_init(sha256_t *s);
void sha256_update(sha256_t *s, const void *data, size_t len);
void sha256_final(sha256_t *s, unsigned char digest[32]);

/* ---- responses (util.c) ---- */

/* Allocates a zeroed response struct that owns `json` (freed on failure). */
void *resp_new(size_t size, ollama_json_t *json);
/* Allocates a zeroed array released together with the response; NULL if count is 0. */
void *resp_alloc(void *resp, size_t count, size_t size);
/* True if a resp_alloc() call failed. */
bool resp_failed(const void *resp);
const ollama_json_t *resp_json(const void *resp);

/* ---- json (json.c) ---- */

ollama_json_t *json_parse_n(const char *text, size_t len);
bool json_write(buf_t *b, const ollama_json_t *value);

/* ---- host (host.c) ---- */

/* Normalizes a host like the Python client's _parse_host() into *out.
 * Returns OLLAMA_ERROR_REQUEST for a host that Python would reject. */
ollama_status_t parse_host(const char *host, char **out);

/* ---- client (client.c) ---- */

typedef struct header {
    char *name;
    char *value;
} header_t;

struct ollama_client {
    char *host;
    char *user_agent;
    CURL *curl;
    header_t *headers;
    size_t headers_count;
    long timeout_ms;
    err_t err;
};

ollama_client_t *client_resolve(ollama_client_t *client);
/* Status to return when client_resolve(NULL) failed. */
ollama_status_t client_resolve_error(void);
const char *client_header(const ollama_client_t *client, const char *name);

/* ---- http (http.c) ---- */

typedef struct http_request {
    const char *method; /* "GET", "POST" or "DELETE" */
    const char *path;   /* path below the host, or an absolute URL */
    const char *body;   /* JSON body, or NULL */
    FILE *upload;       /* raw body read from a file, instead of body */
    uint64_t upload_size;
} http_request_t;

/* Performs a blocking request; on success `out` holds the response body. */
ollama_status_t http_perform(ollama_client_t *client, const http_request_t *request, buf_t *out);

typedef enum stream_kind {
    STREAM_GENERATE,
    STREAM_CHAT,
    STREAM_PROGRESS
} stream_kind_t;

struct ollama_stream {
    stream_kind_t kind;
    CURL *curl;
    CURLM *multi;
    struct curl_slist *headers;
    char *body;
    char errbuf[CURL_ERROR_SIZE];
    buf_t data;
    size_t pos;
    bool finished;
    CURLcode result;
    err_t err;
    void *current;

    /* chat streams: the message assembled from the parts */
    buf_t content;
    buf_t thinking;
    char *role;
    ollama_tool_call_t *tool_calls;
    size_t tool_calls_count;
    size_t tool_calls_cap;
    ollama_message_t message;
};

/* Allocates a stream that has not started yet (api.c). */
ollama_stream_t *stream_new(stream_kind_t kind);
/* Starts a streaming POST request; takes ownership of `body`. */
ollama_status_t http_stream_start(ollama_client_t *client, const char *path, char *body, ollama_stream_t *stream);
/* Next non-empty line: 1 = line, 0 = end of stream, -1 = error (stream->err is set). */
int http_stream_line(ollama_stream_t *stream, const char **line, size_t *len);
void http_stream_close(ollama_stream_t *stream);

/* ---- request serialization (request.c) ---- */

ollama_status_t build_generate(const ollama_generate_request_t *r, bool stream, err_t *e, ollama_json_t **out);
ollama_status_t build_chat(const ollama_chat_request_t *r, bool stream, err_t *e, ollama_json_t **out);
ollama_status_t build_embed(const ollama_embed_request_t *r, err_t *e, ollama_json_t **out);
ollama_status_t build_embeddings(const ollama_embeddings_request_t *r, err_t *e, ollama_json_t **out);
ollama_status_t build_pull_push(const char *model, bool insecure, bool stream, err_t *e, ollama_json_t **out);
ollama_status_t build_create(const ollama_create_request_t *r, bool stream, err_t *e, ollama_json_t **out);
ollama_status_t build_systemone(const ollama_systemone_request_t *r, err_t *e, ollama_json_t **out);

/* ---- response parsing (response.c); they take ownership of `json` ---- */

void *parse_generate(ollama_json_t *json);
void *parse_chat(ollama_json_t *json);
void *parse_embed(ollama_json_t *json);
void *parse_embeddings(ollama_json_t *json);
void *parse_progress(ollama_json_t *json);
void *parse_list(ollama_json_t *json);
void *parse_show(ollama_json_t *json);
void *parse_ps(ollama_json_t *json);
void *parse_web_search(ollama_json_t *json);
void *parse_web_fetch(ollama_json_t *json);
void *parse_systemone(ollama_json_t *json);

/* ---- messages (helpers.c) ---- */

bool message_copy(ollama_message_t *dst, const ollama_message_t *src);
void message_release(ollama_message_t *m);
bool tool_call_copy(ollama_tool_call_t *dst, const ollama_tool_call_t *src);
void tool_call_release(ollama_tool_call_t *t);

#endif /* OLLAMA_INTERNAL_H */
