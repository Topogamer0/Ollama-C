/* HTTP transport on top of libcurl: blocking requests and NDJSON streams. */
#include "internal.h"

#include <stdlib.h>
#include <string.h>

static const char CONNECTION_ERROR_MESSAGE[] =
    "Failed to connect to Ollama. Please check that Ollama is downloaded, running and accessible. "
    "https://ollama.com/download";

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t n = size * nmemb;
    return buf_append(userdata, ptr, n) ? n : 0;
}

static size_t read_cb(char *buffer, size_t size, size_t nitems, void *userdata) {
    FILE *f = userdata;
    size_t n = fread(buffer, 1, size * nitems, f);
    if (n == 0 && ferror(f)) return CURL_READFUNC_ABORT;
    return n;
}

static bool add_header(struct curl_slist **list, const char *name, const char *value) {
    char *line = value[0] ? ol_format("%s: %s", name, value) : ol_format("%s;", name);
    if (!line) return false;
    struct curl_slist *next = curl_slist_append(*list, line);
    free(line);
    if (!next) return false;
    *list = next;
    return true;
}

static ollama_status_t transport_error(err_t *e, CURLcode rc, const char *errbuf, bool oom) {
    if (oom || rc == CURLE_OUT_OF_MEMORY) return err_set(e, OLLAMA_ERROR_MEMORY, -1, "out of memory");
    switch (rc) {
    case CURLE_COULDNT_CONNECT:
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_COULDNT_RESOLVE_PROXY: return err_set(e, OLLAMA_ERROR_CONNECTION, -1, "%s", CONNECTION_ERROR_MESSAGE);
    default: return err_set(e, OLLAMA_ERROR_TRANSPORT, -1, "%s", errbuf[0] ? errbuf : curl_easy_strerror(rc));
    }
}

/* Configures a handle for a request. The header list must outlive the transfer. */
static ollama_status_t setup(CURL *h, const ollama_client_t *c, const http_request_t *r,
                             struct curl_slist **headers, char *errbuf, err_t *e) {
    bool absolute = strncmp(r->path, "http://", 7) == 0 || strncmp(r->path, "https://", 8) == 0;
    char *url = absolute ? ol_strdup(r->path) : ol_format("%s%s", c->host, r->path);
    if (!url) return err_set(e, OLLAMA_ERROR_MEMORY, -1, "out of memory");

    /* headers forced by the Python client, then the custom ones */
    bool ok = add_header(headers, "Content-Type", r->upload ? "application/octet-stream" : "application/json") &&
              add_header(headers, "Accept", "application/json") &&
              add_header(headers, "User-Agent", c->user_agent);
    for (size_t i = 0; ok && i < c->headers_count; i++) {
        const char *name = c->headers[i].name;
        if (ascii_ieq(name, "Content-Type") || ascii_ieq(name, "Accept") || ascii_ieq(name, "User-Agent")) continue;
        ok = add_header(headers, name, c->headers[i].value);
    }
    struct curl_slist *expect = ok ? curl_slist_append(*headers, "Expect:") : NULL;
    if (expect) *headers = expect;
    if (!expect) {
        free(url);
        return err_set(e, OLLAMA_ERROR_MEMORY, -1, "out of memory");
    }

    errbuf[0] = '\0';
    curl_easy_setopt(h, CURLOPT_URL, url);
    free(url);
    curl_easy_setopt(h, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(h, CURLOPT_HTTPHEADER, *headers);
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h, CURLOPT_MAXREDIRS, 20L);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    if (c->timeout_ms > 0) {
        curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS, c->timeout_ms);
        curl_easy_setopt(h, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(h, CURLOPT_LOW_SPEED_TIME, (c->timeout_ms + 999) / 1000);
    }

    if (strcmp(r->method, "GET") == 0) {
        curl_easy_setopt(h, CURLOPT_HTTPGET, 1L);
    } else {
        if (r->upload) {
            curl_easy_setopt(h, CURLOPT_POST, 1L);
            curl_easy_setopt(h, CURLOPT_READFUNCTION, read_cb);
            curl_easy_setopt(h, CURLOPT_READDATA, r->upload);
            curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)r->upload_size);
        } else {
            const char *body = r->body ? r->body : "";
            curl_easy_setopt(h, CURLOPT_POSTFIELDS, body);
            curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)strlen(body));
        }
        if (strcmp(r->method, "POST") != 0) curl_easy_setopt(h, CURLOPT_CUSTOMREQUEST, r->method);
    }
    return OLLAMA_OK;
}

static bool is_success(long status) { return status >= 200 && status < 300; }

ollama_status_t http_perform(ollama_client_t *c, const http_request_t *r, buf_t *out) {
    CURL *h = c->curl;
    char errbuf[CURL_ERROR_SIZE];
    struct curl_slist *headers = NULL;

    curl_easy_reset(h);
    ollama_status_t st = setup(h, c, r, &headers, errbuf, &c->err);
    if (st != OLLAMA_OK) {
        curl_slist_free_all(headers);
        return st;
    }
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, out);

    CURLcode rc = curl_easy_perform(h);
    long status = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    /* do not leave pointers to freed memory in the reused handle */
    curl_easy_setopt(h, CURLOPT_HTTPHEADER, NULL);
    curl_easy_setopt(h, CURLOPT_ERRORBUFFER, NULL);
    curl_slist_free_all(headers);

    if (rc != CURLE_OK) return transport_error(&c->err, rc, errbuf, out->oom);
    if (!is_success(status)) return err_set_response(&c->err, out->data, out->len, status);
    if (!buf_cstr(out)) return err_set(&c->err, OLLAMA_ERROR_MEMORY, -1, "out of memory");
    return OLLAMA_OK;
}

/* ---- streams ---- */

/* Lets the transfer progress, waiting up to a second for activity. */
static bool pump(ollama_stream_t *s, err_t *e) {
    int running = 0;
    CURLMcode mc = curl_multi_perform(s->multi, &running);
    if (mc == CURLM_OK && running) mc = curl_multi_wait(s->multi, NULL, 0, 1000, NULL);
    if (mc != CURLM_OK) {
        err_set(e, OLLAMA_ERROR_TRANSPORT, -1, "%s", curl_multi_strerror(mc));
        return false;
    }
    CURLMsg *msg;
    int left;
    while ((msg = curl_multi_info_read(s->multi, &left)) != NULL) {
        if (msg->msg == CURLMSG_DONE) {
            s->finished = true;
            s->result = msg->data.result;
        }
    }
    if (!running && !s->finished) {
        /* nothing left to do: the DONE message has already been consumed */
        s->finished = true;
    }
    return true;
}

ollama_status_t http_stream_start(ollama_client_t *c, const char *path, char *body, ollama_stream_t *s) {
    s->body = body;
    s->curl = curl_easy_init();
    s->multi = curl_multi_init();
    if (!s->curl || !s->multi) return err_set(&c->err, OLLAMA_ERROR_MEMORY, -1, "out of memory");

    http_request_t r = {"POST", path, body, NULL, 0};
    ollama_status_t st = setup(s->curl, c, &r, &s->headers, s->errbuf, &c->err);
    if (st != OLLAMA_OK) return st;
    curl_easy_setopt(s->curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(s->curl, CURLOPT_WRITEDATA, &s->data);
    if (curl_multi_add_handle(s->multi, s->curl) != CURLM_OK)
        return err_set(&c->err, OLLAMA_ERROR_MEMORY, -1, "out of memory");

    /* wait for the first bytes, so that HTTP errors are reported right away */
    while (s->data.len == 0 && !s->finished)
        if (!pump(s, &c->err)) return c->err.pub.code;
    if (s->finished && s->result != CURLE_OK) return transport_error(&c->err, s->result, s->errbuf, s->data.oom);

    long status = 0;
    curl_easy_getinfo(s->curl, CURLINFO_RESPONSE_CODE, &status);
    if (!is_success(status)) {
        while (!s->finished)
            if (!pump(s, &c->err)) return c->err.pub.code;
        return err_set_response(&c->err, s->data.data, s->data.len, status);
    }
    return OLLAMA_OK;
}

static bool is_blank(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (s[i] != ' ' && s[i] != '\t' && s[i] != '\r') return false;
    return true;
}

int http_stream_line(ollama_stream_t *s, const char **line, size_t *len) {
    for (;;) {
        if (s->data.oom) {
            err_set(&s->err, OLLAMA_ERROR_MEMORY, -1, "out of memory");
            return -1;
        }
        char *start = s->data.data + s->pos;
        size_t avail = s->data.len - s->pos;
        char *nl = avail ? memchr(start, '\n', avail) : NULL;
        if (!nl && s->finished) {
            if (s->result != CURLE_OK) {
                transport_error(&s->err, s->result, s->errbuf, false);
                return -1;
            }
            if (avail == 0) return 0;
        }
        if (nl || s->finished) {
            /* a complete line, or the last one without a trailing newline */
            size_t n = nl ? (size_t)(nl - start) : avail;
            s->pos += nl ? n + 1 : n;
            start[n] = '\0';
            if (is_blank(start, n)) continue;
            *line = start;
            *len = n;
            return 1;
        }
        if (s->pos > 0) {
            memmove(s->data.data, start, avail);
            s->data.len = avail;
            s->pos = 0;
        }
        if (!pump(s, &s->err)) return -1;
    }
}

void http_stream_close(ollama_stream_t *s) {
    if (s->multi) {
        if (s->curl) curl_multi_remove_handle(s->multi, s->curl);
        curl_multi_cleanup(s->multi);
    }
    if (s->curl) curl_easy_cleanup(s->curl);
    curl_slist_free_all(s->headers);
    free(s->body);
    buf_free(&s->data);
    s->multi = NULL;
    s->curl = NULL;
    s->headers = NULL;
    s->body = NULL;
}
