/* Client lifecycle and configuration. */
#include "internal.h"

#include <stdlib.h>
#include <string.h>

#if defined(__x86_64__) || defined(_M_X64)
#define OLLAMA_ARCH "x86_64"
#elif defined(__aarch64__) || defined(_M_ARM64)
#define OLLAMA_ARCH "arm64"
#elif defined(__i386__) || defined(_M_IX86)
#define OLLAMA_ARCH "x86"
#elif defined(__arm__) || defined(_M_ARM)
#define OLLAMA_ARCH "arm"
#else
#define OLLAMA_ARCH "unknown"
#endif

#if defined(_WIN32)
#define OLLAMA_OS "windows"
#elif defined(__APPLE__)
#define OLLAMA_OS "darwin"
#elif defined(__linux__)
#define OLLAMA_OS "linux"
#elif defined(__FreeBSD__)
#define OLLAMA_OS "freebsd"
#else
#define OLLAMA_OS "unknown"
#endif

static ollama_client_t *default_client;
static bool default_client_failed;

ollama_client_t *ollama_client_new(const char *host) {
    ollama_client_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    err_init(&c->err);

    if (!host || !*host) host = getenv("OLLAMA_HOST");
    c->host = parse_host(host);
    c->user_agent = ol_format("ollama-c/%s (%s %s) libcurl/%s", OLLAMA_C_VERSION, OLLAMA_ARCH, OLLAMA_OS,
                              curl_version_info(CURLVERSION_NOW)->version);
    c->curl = curl_easy_init();
    if (!c->host || !c->user_agent || !c->curl) {
        ollama_client_free(c);
        return NULL;
    }

    const char *api_key = getenv("OLLAMA_API_KEY");
    if (api_key && *api_key) {
        char *bearer = ol_format("Bearer %s", api_key);
        ollama_status_t st = bearer ? ollama_client_set_header(c, "Authorization", bearer) : OLLAMA_ERROR_MEMORY;
        free(bearer);
        if (st != OLLAMA_OK) {
            ollama_client_free(c);
            return NULL;
        }
    }
    return c;
}

void ollama_client_free(ollama_client_t *c) {
    if (!c) return;
    if (c == default_client) default_client = NULL;
    if (c->curl) curl_easy_cleanup(c->curl);
    for (size_t i = 0; i < c->headers_count; i++) {
        free(c->headers[i].name);
        free(c->headers[i].value);
    }
    free(c->headers);
    free(c->host);
    free(c->user_agent);
    err_free(&c->err);
    free(c);
}

ollama_client_t *client_resolve(ollama_client_t *c) {
    if (c) return c;
    if (!default_client) {
        default_client = ollama_client_new(NULL);
        default_client_failed = default_client == NULL;
    }
    return default_client;
}

void ollama_cleanup(void) {
    ollama_client_free(default_client);
    default_client_failed = false;
}

const char *ollama_client_host(const ollama_client_t *c) {
    c = c ? c : client_resolve(NULL);
    return c ? c->host : NULL;
}

const char *client_header(const ollama_client_t *c, const char *name) {
    for (size_t i = 0; i < c->headers_count; i++)
        if (ascii_ieq(c->headers[i].name, name)) return c->headers[i].value;
    return NULL;
}

ollama_status_t ollama_client_set_header(ollama_client_t *c, const char *name, const char *value) {
    c = client_resolve(c);
    if (!c) return OLLAMA_ERROR_MEMORY;
    if (!name || !*name || strpbrk(name, ":\r\n") || (value && strpbrk(value, "\r\n")))
        return err_set(&c->err, OLLAMA_ERROR_REQUEST, -1, "invalid header");

    size_t i = 0;
    while (i < c->headers_count && !ascii_ieq(c->headers[i].name, name)) i++;

    if (!value) {
        if (i < c->headers_count) {
            free(c->headers[i].name);
            free(c->headers[i].value);
            c->headers[i] = c->headers[--c->headers_count];
        }
        return OLLAMA_OK;
    }

    char *copy = ol_strdup(value);
    if (!copy) return err_set(&c->err, OLLAMA_ERROR_MEMORY, -1, "out of memory");
    if (i < c->headers_count) {
        free(c->headers[i].value);
        c->headers[i].value = copy;
        return OLLAMA_OK;
    }
    char *name_copy = ol_strdup(name);
    header_t *headers = name_copy ? realloc(c->headers, (c->headers_count + 1) * sizeof(*headers)) : NULL;
    if (!headers) {
        free(copy);
        free(name_copy);
        return err_set(&c->err, OLLAMA_ERROR_MEMORY, -1, "out of memory");
    }
    c->headers = headers;
    c->headers[c->headers_count].name = name_copy;
    c->headers[c->headers_count].value = copy;
    c->headers_count++;
    return OLLAMA_OK;
}

void ollama_client_set_timeout(ollama_client_t *c, long timeout_ms) {
    c = client_resolve(c);
    if (c) c->timeout_ms = timeout_ms > 0 ? timeout_ms : 0;
}

const ollama_error_t *ollama_last_error(const ollama_client_t *c) {
    static const ollama_error_t no_client = {OLLAMA_ERROR_MEMORY, -1, "out of memory"};
    static const ollama_error_t no_error = {OLLAMA_OK, -1, ""};
    if (!c) c = default_client;
    if (!c) return default_client_failed ? &no_client : &no_error;
    return &c->err.pub;
}
