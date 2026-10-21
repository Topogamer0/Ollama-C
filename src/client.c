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
/* why the default client could not be created */
static ollama_error_t default_client_error = {OLLAMA_OK, -1, ""};

/* Creates a client; on failure *status tells whether the host was invalid or memory ran out. */
static ollama_client_t *client_create(const char *host, ollama_status_t *status) {
    *status = OLLAMA_ERROR_MEMORY;
    ollama_client_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    err_init(&c->err);

    if (!host || !*host) host = getenv("OLLAMA_HOST");
    ollama_status_t st = parse_host(host, &c->host);
    if (st != OLLAMA_OK) {
        *status = st;
        ollama_client_free(c);
        return NULL;
    }
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
        st = bearer ? ollama_client_set_header(c, "Authorization", bearer) : OLLAMA_ERROR_MEMORY;
        free(bearer);
        if (st != OLLAMA_OK) {
            *status = st;
            ollama_client_free(c);
            return NULL;
        }
    }
    *status = OLLAMA_OK;
    return c;
}

ollama_client_t *ollama_client_new(const char *host) {
    ollama_status_t status;
    return client_create(host, &status);
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
        ollama_status_t status;
        default_client = client_create(NULL, &status);
        default_client_error.code = status;
        default_client_error.message = status == OLLAMA_OK            ? ""
                                       : status == OLLAMA_ERROR_REQUEST ? "invalid OLLAMA_HOST or OLLAMA_API_KEY"
                                                                        : "out of memory";
    }
    return default_client;
}

ollama_status_t client_resolve_error(void) {
    return default_client_error.code != OLLAMA_OK ? default_client_error.code : OLLAMA_ERROR_MEMORY;
}

void ollama_cleanup(void) {
    ollama_client_free(default_client);
    default_client_error.code = OLLAMA_OK;
    default_client_error.message = "";
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
    if (!c) return client_resolve_error();
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
    if (!c) c = default_client;
    return c ? &c->err.pub : &default_client_error;
}
