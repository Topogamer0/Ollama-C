/* The public API: one function per method of the Python Client, plus streams. */
#include "internal.h"

#include <stdlib.h>
#include <string.h>

typedef void *(*parse_fn)(ollama_json_t *json);

static ollama_status_t oom(err_t *e) { return err_set(e, OLLAMA_ERROR_MEMORY, -1, "out of memory"); }

/* Resolves the client (NULL: default client) and clears its last error. */
static ollama_client_t *begin(ollama_client_t *client) {
    ollama_client_t *c = client_resolve(client);
    if (c) err_clear(&c->err);
    return c;
}

/* Sends a request with an optional JSON body (consumed) and parses the JSON answer into *out. */
static ollama_status_t request(ollama_client_t *c, const char *method, const char *path, ollama_json_t *body,
                               ollama_json_t **out) {
    char *text = NULL;
    if (body) {
        text = ollama_json_stringify(body);
        ollama_json_free(body);
        if (!text) return oom(&c->err);
    }
    buf_t res = {0};
    http_request_t r = {method, path, text, NULL, 0};
    ollama_status_t st = http_perform(c, &r, &res);
    free(text);
    if (st == OLLAMA_OK && out) {
        *out = json_parse_n(res.data, res.len);
        if (!*out) st = err_set(&c->err, OLLAMA_ERROR_PARSE, -1, "invalid JSON response: %.200s", res.data);
    }
    buf_free(&res);
    return st;
}

/* request() followed by the conversion of the answer into a response struct. */
static ollama_status_t call(ollama_client_t *c, const char *method, const char *path, ollama_json_t *body,
                            parse_fn parse, void **response) {
    ollama_json_t *json = NULL;
    ollama_status_t st = request(c, method, path, body, &json);
    if (st != OLLAMA_OK) return st;
    *response = parse(json);
    return *response ? OLLAMA_OK : oom(&c->err);
}

#define CHECK_OUT(c, out)                                                                  \
    do {                                                                                   \
        if (!(c)) return OLLAMA_ERROR_MEMORY;                                              \
        if (!(out)) return err_set(&(c)->err, OLLAMA_ERROR_REQUEST, -1, #out " is NULL"); \
    } while (0)

/* ---- blocking calls ---- */

ollama_status_t ollama_generate(ollama_client_t *client, const ollama_generate_request_t *req,
                                ollama_generate_response_t **response) {
    if (response) *response = NULL;
    ollama_client_t *c = begin(client);
    CHECK_OUT(c, response);
    ollama_json_t *body;
    void *r = NULL;
    ollama_status_t st = build_generate(req, false, &c->err, &body);
    if (st == OLLAMA_OK) st = call(c, "POST", "/api/generate", body, parse_generate, &r);
    *response = r;
    return st;
}

ollama_status_t ollama_chat(ollama_client_t *client, const ollama_chat_request_t *req,
                            ollama_chat_response_t **response) {
    if (response) *response = NULL;
    ollama_client_t *c = begin(client);
    CHECK_OUT(c, response);
    ollama_json_t *body;
    void *r = NULL;
    ollama_status_t st = build_chat(req, false, &c->err, &body);
    if (st == OLLAMA_OK) st = call(c, "POST", "/api/chat", body, parse_chat, &r);
    *response = r;
    return st;
}

ollama_status_t ollama_embed(ollama_client_t *client, const ollama_embed_request_t *req,
                             ollama_embed_response_t **response) {
    if (response) *response = NULL;
    ollama_client_t *c = begin(client);
    CHECK_OUT(c, response);
    ollama_json_t *body;
    void *r = NULL;
    ollama_status_t st = build_embed(req, &c->err, &body);
    if (st == OLLAMA_OK) st = call(c, "POST", "/api/embed", body, parse_embed, &r);
    *response = r;
    return st;
}

ollama_status_t ollama_embeddings(ollama_client_t *client, const ollama_embeddings_request_t *req,
                                  ollama_embeddings_response_t **response) {
    if (response) *response = NULL;
    ollama_client_t *c = begin(client);
    CHECK_OUT(c, response);
    ollama_json_t *body;
    void *r = NULL;
    ollama_status_t st = build_embeddings(req, &c->err, &body);
    if (st == OLLAMA_OK) st = call(c, "POST", "/api/embeddings", body, parse_embeddings, &r);
    *response = r;
    return st;
}

static ollama_status_t pull_push(ollama_client_t *client, const char *path, const char *model, bool insecure,
                                 ollama_progress_response_t **response) {
    if (response) *response = NULL;
    ollama_client_t *c = begin(client);
    CHECK_OUT(c, response);
    ollama_json_t *body;
    void *r = NULL;
    ollama_status_t st = build_pull_push(model, insecure, false, &c->err, &body);
    if (st == OLLAMA_OK) st = call(c, "POST", path, body, parse_progress, &r);
    *response = r;
    return st;
}

ollama_status_t ollama_pull(ollama_client_t *client, const char *model, bool insecure,
                            ollama_progress_response_t **response) {
    return pull_push(client, "/api/pull", model, insecure, response);
}

ollama_status_t ollama_push(ollama_client_t *client, const char *model, bool insecure,
                            ollama_progress_response_t **response) {
    return pull_push(client, "/api/push", model, insecure, response);
}

ollama_status_t ollama_create(ollama_client_t *client, const ollama_create_request_t *req,
                              ollama_progress_response_t **response) {
    if (response) *response = NULL;
    ollama_client_t *c = begin(client);
    CHECK_OUT(c, response);
    ollama_json_t *body;
    void *r = NULL;
    ollama_status_t st = build_create(req, false, &c->err, &body);
    if (st == OLLAMA_OK) st = call(c, "POST", "/api/create", body, parse_progress, &r);
    *response = r;
    return st;
}

ollama_status_t ollama_create_blob(ollama_client_t *client, const char *path, char **digest) {
    if (digest) *digest = NULL;
    ollama_client_t *c = begin(client);
    CHECK_OUT(c, digest);
    if (!path) return err_set(&c->err, OLLAMA_ERROR_REQUEST, -1, "path is NULL");

    FILE *f = fopen(path, "rb");
    if (!f) return err_set(&c->err, OLLAMA_ERROR_IO, -1, "could not open file '%s'", path);

    /* hash the file, then upload it from the start */
    sha256_t sha;
    sha256_init(&sha);
    uint64_t size = 0;
    char chunk[32 * 1024];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        sha256_update(&sha, chunk, n);
        size += n;
    }
    if (ferror(f) || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return err_set(&c->err, OLLAMA_ERROR_IO, -1, "could not read file '%s'", path);
    }
    unsigned char hash[32];
    sha256_final(&sha, hash);
    char hex[7 + 64 + 1] = "sha256:";
    for (int i = 0; i < 32; i++) snprintf(hex + 7 + 2 * i, 3, "%02x", hash[i]);

    char *url = ol_format("/api/blobs/%s", hex);
    ollama_status_t st;
    if (url) {
        buf_t res = {0};
        http_request_t r = {"POST", url, NULL, f, size};
        st = http_perform(c, &r, &res);
        buf_free(&res);
        free(url);
    } else {
        st = oom(&c->err);
    }
    fclose(f);
    if (st == OLLAMA_OK && !(*digest = ol_strdup(hex))) st = oom(&c->err);
    return st;
}

ollama_status_t ollama_list(ollama_client_t *client, ollama_list_response_t **response) {
    if (response) *response = NULL;
    ollama_client_t *c = begin(client);
    CHECK_OUT(c, response);
    void *r = NULL;
    ollama_status_t st = call(c, "GET", "/api/tags", NULL, parse_list, &r);
    *response = r;
    return st;
}

/* Builds {"model": model}, used by delete and show. */
static ollama_status_t model_body(ollama_client_t *c, const char *model, ollama_json_t **body) {
    *body = NULL;
    if (!model || !*model) return err_set(&c->err, OLLAMA_ERROR_REQUEST, -1, "must provide a model");
    *body = ollama_json_object();
    if (!ollama_json_set_string(*body, "model", model)) {
        ollama_json_free(*body);
        *body = NULL;
        return oom(&c->err);
    }
    return OLLAMA_OK;
}

ollama_status_t ollama_delete(ollama_client_t *client, const char *model) {
    ollama_client_t *c = begin(client);
    if (!c) return OLLAMA_ERROR_MEMORY;
    ollama_json_t *body;
    ollama_status_t st = model_body(c, model, &body);
    if (st == OLLAMA_OK) st = request(c, "DELETE", "/api/delete", body, NULL);
    return st;
}

ollama_status_t ollama_copy(ollama_client_t *client, const char *source, const char *destination) {
    ollama_client_t *c = begin(client);
    if (!c) return OLLAMA_ERROR_MEMORY;
    if (!source || !destination)
        return err_set(&c->err, OLLAMA_ERROR_REQUEST, -1, "must provide a source and a destination");
    ollama_json_t *body = ollama_json_object();
    if (!ollama_json_set_string(body, "source", source) || !ollama_json_set_string(body, "destination", destination)) {
        ollama_json_free(body);
        return oom(&c->err);
    }
    return request(c, "POST", "/api/copy", body, NULL);
}

ollama_status_t ollama_show(ollama_client_t *client, const char *model, ollama_show_response_t **response) {
    if (response) *response = NULL;
    ollama_client_t *c = begin(client);
    CHECK_OUT(c, response);
    ollama_json_t *body;
    void *r = NULL;
    ollama_status_t st = model_body(c, model, &body);
    if (st == OLLAMA_OK) st = call(c, "POST", "/api/show", body, parse_show, &r);
    *response = r;
    return st;
}

ollama_status_t ollama_ps(ollama_client_t *client, ollama_process_response_t **response) {
    if (response) *response = NULL;
    ollama_client_t *c = begin(client);
    CHECK_OUT(c, response);
    void *r = NULL;
    ollama_status_t st = call(c, "GET", "/api/ps", NULL, parse_ps, &r);
    *response = r;
    return st;
}

static ollama_status_t require_bearer(ollama_client_t *c, const char *what) {
    const char *auth = client_header(c, "Authorization");
    if (!auth || strncmp(auth, "Bearer ", 7) != 0)
        return err_set(&c->err, OLLAMA_ERROR_REQUEST, -1, "Authorization header with Bearer token is required for %s",
                       what);
    return OLLAMA_OK;
}

ollama_status_t ollama_web_search(ollama_client_t *client, const char *query, int max_results,
                                  ollama_web_search_response_t **response) {
    if (response) *response = NULL;
    ollama_client_t *c = begin(client);
    CHECK_OUT(c, response);
    if (!query) return err_set(&c->err, OLLAMA_ERROR_REQUEST, -1, "must provide a query");
    ollama_status_t st = require_bearer(c, "web search");
    if (st != OLLAMA_OK) return st;
    ollama_json_t *body = ollama_json_object();
    if (!ollama_json_set_string(body, "query", query) ||
        !ollama_json_set_int(body, "max_results", max_results > 0 ? max_results : 3)) {
        ollama_json_free(body);
        return oom(&c->err);
    }
    void *r = NULL;
    st = call(c, "POST", "https://ollama.com/api/web_search", body, parse_web_search, &r);
    *response = r;
    return st;
}

ollama_status_t ollama_web_fetch(ollama_client_t *client, const char *url, ollama_web_fetch_response_t **response) {
    if (response) *response = NULL;
    ollama_client_t *c = begin(client);
    CHECK_OUT(c, response);
    if (!url) return err_set(&c->err, OLLAMA_ERROR_REQUEST, -1, "must provide a url");
    ollama_status_t st = require_bearer(c, "web fetch");
    if (st != OLLAMA_OK) return st;
    ollama_json_t *body = ollama_json_object();
    if (!ollama_json_set_string(body, "url", url)) {
        ollama_json_free(body);
        return oom(&c->err);
    }
    void *r = NULL;
    st = call(c, "POST", "https://ollama.com/api/web_fetch", body, parse_web_fetch, &r);
    *response = r;
    return st;
}

ollama_status_t ollama_systemone(ollama_client_t *client, const ollama_systemone_request_t *req,
                                 ollama_systemone_response_t **response) {
    if (response) *response = NULL;
    ollama_client_t *c = begin(client);
    CHECK_OUT(c, response);
    ollama_json_t *body;
    void *r = NULL;
    ollama_status_t st = build_systemone(req, &c->err, &body);
    if (st == OLLAMA_OK) st = call(c, "POST", "/v1/systemone", body, parse_systemone, &r);
    *response = r;
    return st;
}

/* ---- streams ---- */

static ollama_status_t open_stream(ollama_client_t *c, const char *path, ollama_json_t *body, stream_kind_t kind,
                                   ollama_stream_t **out) {
    char *text = ollama_json_stringify(body);
    ollama_json_free(body);
    ollama_stream_t *s = text ? calloc(1, sizeof(*s)) : NULL;
    if (!s) {
        free(text);
        return oom(&c->err);
    }
    s->kind = kind;
    err_init(&s->err);
    if (kind == STREAM_CHAT) {
        s->message.role = "assistant";
        s->message.content = "";
    }
    ollama_status_t st = http_stream_start(c, path, text, s);
    if (st != OLLAMA_OK) {
        ollama_stream_free(s);
        return st;
    }
    *out = s;
    return OLLAMA_OK;
}

ollama_status_t ollama_generate_stream(ollama_client_t *client, const ollama_generate_request_t *req,
                                       ollama_stream_t **stream) {
    if (stream) *stream = NULL;
    ollama_client_t *c = begin(client);
    CHECK_OUT(c, stream);
    ollama_json_t *body;
    ollama_status_t st = build_generate(req, true, &c->err, &body);
    return st == OLLAMA_OK ? open_stream(c, "/api/generate", body, STREAM_GENERATE, stream) : st;
}

ollama_status_t ollama_chat_stream(ollama_client_t *client, const ollama_chat_request_t *req,
                                   ollama_stream_t **stream) {
    if (stream) *stream = NULL;
    ollama_client_t *c = begin(client);
    CHECK_OUT(c, stream);
    ollama_json_t *body;
    ollama_status_t st = build_chat(req, true, &c->err, &body);
    return st == OLLAMA_OK ? open_stream(c, "/api/chat", body, STREAM_CHAT, stream) : st;
}

static ollama_status_t pull_push_stream(ollama_client_t *client, const char *path, const char *model, bool insecure,
                                        ollama_stream_t **stream) {
    if (stream) *stream = NULL;
    ollama_client_t *c = begin(client);
    CHECK_OUT(c, stream);
    ollama_json_t *body;
    ollama_status_t st = build_pull_push(model, insecure, true, &c->err, &body);
    return st == OLLAMA_OK ? open_stream(c, path, body, STREAM_PROGRESS, stream) : st;
}

ollama_status_t ollama_pull_stream(ollama_client_t *client, const char *model, bool insecure,
                                   ollama_stream_t **stream) {
    return pull_push_stream(client, "/api/pull", model, insecure, stream);
}

ollama_status_t ollama_push_stream(ollama_client_t *client, const char *model, bool insecure,
                                   ollama_stream_t **stream) {
    return pull_push_stream(client, "/api/push", model, insecure, stream);
}

ollama_status_t ollama_create_stream(ollama_client_t *client, const ollama_create_request_t *req,
                                     ollama_stream_t **stream) {
    if (stream) *stream = NULL;
    ollama_client_t *c = begin(client);
    CHECK_OUT(c, stream);
    ollama_json_t *body;
    ollama_status_t st = build_create(req, true, &c->err, &body);
    return st == OLLAMA_OK ? open_stream(c, "/api/create", body, STREAM_PROGRESS, stream) : st;
}

/* Adds a chat part to the message assembled by the stream. */
static bool accumulate(ollama_stream_t *s, const ollama_chat_response_t *part) {
    const ollama_message_t *m = &part->message;
    if (!s->role && m->role && *m->role && !(s->role = ol_strdup(m->role))) return false;
    if (m->content && !buf_puts(&s->content, m->content)) return false;
    if (m->thinking && !buf_puts(&s->thinking, m->thinking)) return false;
    for (size_t i = 0; i < m->tool_calls_count; i++) {
        if (s->tool_calls_count == s->tool_calls_cap) {
            size_t cap = s->tool_calls_cap ? s->tool_calls_cap * 2 : 4;
            ollama_tool_call_t *calls = realloc(s->tool_calls, cap * sizeof(*calls));
            if (!calls) return false;
            s->tool_calls = calls;
            s->tool_calls_cap = cap;
        }
        if (!tool_call_copy(&s->tool_calls[s->tool_calls_count], &m->tool_calls[i])) return false;
        s->tool_calls_count++;
    }
    const char *content = buf_cstr(&s->content);
    if (!content) return false;
    s->message.role = s->role ? s->role : "assistant";
    s->message.content = content;
    s->message.thinking = s->thinking.len ? buf_cstr(&s->thinking) : NULL;
    s->message.tool_calls = s->tool_calls;
    s->message.tool_calls_count = s->tool_calls_count;
    return true;
}

static void *stream_next(ollama_stream_t *s, stream_kind_t kind) {
    if (!s) return NULL;
    ollama_response_free(s->current);
    s->current = NULL;
    if (s->err.pub.code != OLLAMA_OK) return NULL;
    if (s->kind != kind) {
        err_set(&s->err, OLLAMA_ERROR_REQUEST, -1, "wrong ollama_stream_next_* function for this stream");
        return NULL;
    }

    const char *line;
    size_t len;
    if (http_stream_line(s, &line, &len) <= 0) return NULL;

    ollama_json_t *json = json_parse_n(line, len);
    if (!json) {
        err_set(&s->err, OLLAMA_ERROR_PARSE, -1, "invalid JSON in stream: %.200s", line);
        return NULL;
    }
    const char *error = ollama_json_as_string(ollama_json_get(json, "error"));
    if (error && *error) {
        err_set(&s->err, OLLAMA_ERROR_RESPONSE, -1, "%s", error);
        ollama_json_free(json);
        return NULL;
    }

    void *part = kind == STREAM_GENERATE ? parse_generate(json)
                 : kind == STREAM_CHAT   ? parse_chat(json)
                                         : parse_progress(json);
    if (part && kind == STREAM_CHAT && !accumulate(s, part)) {
        ollama_response_free(part);
        part = NULL;
    }
    if (!part) {
        oom(&s->err);
        return NULL;
    }
    s->current = part;
    return part;
}

const ollama_generate_response_t *ollama_stream_next_generate(ollama_stream_t *stream) {
    return stream_next(stream, STREAM_GENERATE);
}

const ollama_chat_response_t *ollama_stream_next_chat(ollama_stream_t *stream) {
    return stream_next(stream, STREAM_CHAT);
}

const ollama_progress_response_t *ollama_stream_next_progress(ollama_stream_t *stream) {
    return stream_next(stream, STREAM_PROGRESS);
}

const ollama_error_t *ollama_stream_error(const ollama_stream_t *stream) {
    static const ollama_error_t null_stream = {OLLAMA_ERROR_REQUEST, -1, "stream is NULL"};
    return stream ? &stream->err.pub : &null_stream;
}

const ollama_message_t *ollama_stream_message(const ollama_stream_t *stream) {
    return stream && stream->kind == STREAM_CHAT ? &stream->message : NULL;
}

void ollama_stream_free(ollama_stream_t *s) {
    if (!s) return;
    ollama_response_free(s->current);
    http_stream_close(s);
    err_free(&s->err);
    buf_free(&s->content);
    buf_free(&s->thinking);
    free(s->role);
    for (size_t i = 0; i < s->tool_calls_count; i++) tool_call_release(&s->tool_calls[i]);
    free(s->tool_calls);
    free(s);
}
