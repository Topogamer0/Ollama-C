/* Host normalization, a port of _parse_host() from the Python client. */
#include "internal.h"

#include <stdlib.h>
#include <string.h>

#define DEFAULT_PORT 11434

static bool is_digit(char c) { return c >= '0' && c <= '9'; }

static bool is_hex(char c) { return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

/* A dotted-quad IPv4 address, without leading zeros like Python's ipaddress. */
static bool valid_ipv4(const char *s, size_t n) {
    int octets = 0;
    size_t i = 0;
    while (octets < 4) {
        size_t start = i;
        unsigned value = 0;
        while (i < n && is_digit(s[i]) && i - start < 3) value = value * 10 + (unsigned)(s[i++] - '0');
        size_t len = i - start;
        if (len == 0 || value > 255 || (len > 1 && s[start] == '0')) return false;
        octets++;
        if (octets < 4) {
            if (i >= n || s[i] != '.') return false;
            i++;
        }
    }
    return i == n;
}

/* An IPv6 address as accepted by Python's ipaddress, with an optional %zone. */
static bool valid_ipv6(const char *s, size_t n) {
    const char *zone = memchr(s, '%', n);
    if (zone) {
        size_t zone_len = n - (size_t)(zone - s) - 1;
        if (zone_len == 0 || memchr(zone + 1, '%', zone_len)) return false;
        n = (size_t)(zone - s);
    }
    int groups = 0;
    bool compressed = false;
    size_t i = 0;
    if (n >= 2 && s[0] == ':' && s[1] == ':') {
        compressed = true;
        i = 2;
    } else if (n > 0 && s[0] == ':') {
        return false;
    }
    while (i < n) {
        size_t start = i;
        while (i < n && s[i] != ':') i++;
        size_t len = i - start;
        if (len == 0) return false;
        if (i == n && memchr(s + start, '.', len)) {
            /* an IPv4 address in the last two groups */
            if (!valid_ipv4(s + start, len)) return false;
            groups += 2;
        } else {
            if (len > 4) return false;
            for (size_t k = start; k < i; k++)
                if (!is_hex(s[k])) return false;
            groups++;
        }
        if (i < n) {
            i++; /* ':' */
            if (i < n && s[i] == ':') {
                if (compressed) return false;
                compressed = true;
                i++;
            } else if (i == n) {
                return false;
            }
        }
    }
    /* "::" stands for at least one group */
    return compressed ? groups <= 7 : groups == 8;
}

/* What urlsplit() accepts between brackets: an IPv6 address or an IPvFuture literal. */
static bool valid_bracketed_host(const char *s, size_t n) {
    if (n > 0 && s[0] == 'v') {
        size_t i = 1;
        while (i < n && is_hex(s[i])) i++;
        if (i == 1 || i >= n || s[i] != '.' || i + 1 >= n) return false;
        return memchr(s, '\n', n) == NULL;
    }
    return valid_ipv6(s, n);
}

/*
 *   NULL / ""                 -> http://127.0.0.1:11434
 *   1.2.3.4:56789             -> http://1.2.3.4:56789
 *   https://example.com       -> https://example.com:443
 *   example.com:56789/path/   -> http://example.com:56789/path
 *   [::1]                     -> http://[::1]:11434
 *
 * Like the Python client, which gets a ValueError from urlsplit(), it rejects
 * unbalanced or invalid brackets and ports that are not numbers up to 65535.
 */
ollama_status_t parse_host(const char *host, char **out) {
    *out = NULL;
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

    /* "Invalid IPv6 URL" */
    const char *open = memchr(hostport, '[', netloc_len);
    const char *close_any = memchr(hostport, ']', netloc_len);
    if ((open == NULL) != (close_any == NULL)) return OLLAMA_ERROR_REQUEST;
    if (open) {
        size_t rest = netloc_len - (size_t)(open + 1 - hostport);
        const char *close = memchr(open + 1, ']', rest);
        if (!close || !valid_bracketed_host(open + 1, (size_t)(close - open - 1))) return OLLAMA_ERROR_REQUEST;
    }

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
    /* the user info had brackets but the host does not, or the other way round */
    if (memchr(name, '[', name_len) || memchr(name, ']', name_len)) return OLLAMA_ERROR_REQUEST;

    if (port_str && port_len > 0) {
        long value = 0;
        for (size_t i = 0; i < port_len; i++) {
            if (!is_digit(port_str[i])) return OLLAMA_ERROR_REQUEST; /* "Port could not be cast to integer" */
            value = value * 10 + (port_str[i] - '0');
            if (value > 65535) return OLLAMA_ERROR_REQUEST; /* "Port out of range 0-65535" */
        }
        if (value > 0) port = value; /* port 0 means the default, as in Python */
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
        return OLLAMA_ERROR_MEMORY;
    }
    *out = b.data;
    return OLLAMA_OK;
}
