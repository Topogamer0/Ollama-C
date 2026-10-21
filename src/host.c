/* Host normalization, a port of _parse_host() from the Python client. */
#include "internal.h"

#include <stdlib.h>
#include <string.h>

#define DEFAULT_PORT 11434

static bool is_ascii_digits(const char *s, size_t n) {
    if (n == 0) return false;
    for (size_t i = 0; i < n; i++)
        if (s[i] < '0' || s[i] > '9') return false;
    return true;
}

/*
 *   NULL / ""                 -> http://127.0.0.1:11434
 *   1.2.3.4:56789             -> http://1.2.3.4:56789
 *   https://example.com       -> https://example.com:443
 *   example.com:56789/path/   -> http://example.com:56789/path
 *   [::1]                     -> http://[::1]:11434
 */
char *parse_host(const char *host) {
    if (!host) host = "";

    const char *scheme = "http";
    size_t scheme_len = 4;
    const char *hostport = host;
    long port = DEFAULT_PORT;

    const char *sep = strstr(host, "://");
    if (sep && sep[3] != '\0') {
        scheme = host;
        scheme_len = (size_t)(sep - host);
        hostport = sep + 3;
        if (scheme_len == 4 && memcmp(scheme, "http", 4) == 0)
            port = 80;
        else if (scheme_len == 5 && memcmp(scheme, "https", 5) == 0)
            port = 443;
    }

    /* netloc ends at the first '/', '?' or '#'; the path ends at '?' or '#' */
    size_t netloc_len = strcspn(hostport, "/?#");
    const char *path = hostport + netloc_len;
    size_t path_len = path[0] == '/' ? strcspn(path, "?#") : 0;

    /* drop the user info */
    const char *hostinfo = hostport;
    for (size_t i = 0; i < netloc_len; i++)
        if (hostport[i] == '@') hostinfo = hostport + i + 1;
    size_t hostinfo_len = netloc_len - (size_t)(hostinfo - hostport);

    const char *name;
    size_t name_len;
    const char *port_str = NULL;
    size_t port_len = 0;
    const char *bracket = memchr(hostinfo, '[', hostinfo_len);
    if (bracket) {
        name = bracket + 1;
        size_t rest = hostinfo_len - (size_t)(name - hostinfo);
        const char *close = memchr(name, ']', rest);
        name_len = close ? (size_t)(close - name) : rest;
        if (close) {
            const char *colon = memchr(close, ':', rest - name_len);
            if (colon) {
                port_str = colon + 1;
                port_len = rest - (size_t)(port_str - name);
            }
        }
    } else {
        name = hostinfo;
        const char *colon = memchr(hostinfo, ':', hostinfo_len);
        name_len = colon ? (size_t)(colon - hostinfo) : hostinfo_len;
        if (colon) {
            port_str = colon + 1;
            port_len = hostinfo_len - name_len - 1;
        }
    }

    if (port_str && is_ascii_digits(port_str, port_len) && port_len <= 5) {
        long p = strtol(port_str, NULL, 10);
        if (p > 0 && p <= 65535) port = p;
    }

    if (name_len == 0) {
        name = "127.0.0.1";
        name_len = strlen(name);
    }
    bool ipv6 = memchr(name, ':', name_len) != NULL;

    /* strip the slashes around the path */
    while (path_len > 0 && path[0] == '/') {
        path++;
        path_len--;
    }
    while (path_len > 0 && path[path_len - 1] == '/') path_len--;

    buf_t b = {0};
    buf_append(&b, scheme, scheme_len);
    buf_puts(&b, "://");
    if (ipv6) buf_putc(&b, '[');
    for (size_t i = 0; i < name_len; i++) {
        char c = name[i];
        buf_putc(&b, c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c);
    }
    if (ipv6) buf_putc(&b, ']');
    char portbuf[16];
    snprintf(portbuf, sizeof(portbuf), ":%ld", port);
    buf_puts(&b, portbuf);
    if (path_len > 0) {
        buf_putc(&b, '/');
        buf_append(&b, path, path_len);
    }
    if (!buf_cstr(&b)) {
        buf_free(&b);
        return NULL;
    }
    return b.data;
}
