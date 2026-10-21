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

/*
 * What urlsplit() accepts between brackets: an IPv6 address or an IPvFuture
 * literal. Python takes any character after "v<hex>."; RFC 3986 only allows
 * unreserved characters, sub-delims and ':', which keeps the URL parseable.
 */
static bool valid_bracketed_host(const char *s, size_t n) {
    if (n > 0 && (s[0] == 'v' || s[0] == 'V')) {
        size_t i = 1;
        while (i < n && is_hex(s[i])) i++;
        if (i == 1 || i >= n || s[i] != '.' || i + 1 >= n) return false;
        for (i++; i < n; i++) {
            char c = s[i];
            bool alnum = is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
            if (!alnum && !strchr("-._~!$&'()*+,;=:", c)) return false;
        }
        return true;
    }
    return valid_ipv6(s, n);
}

/* Builds the URL from the host and port part of the address (after "scheme://"). */
static ollama_status_t parse_hostport(const char *scheme, size_t scheme_len, long port, const char *hostport,
                                      char **out) {
    /* netloc ends at the first '/', '?' or '#'; the path ends at '?' or '#' */
    size_t netloc_len = strcspn(hostport, "/?#");
    const char *path = hostport + netloc_len;
    size_t path_len = path[0] == '/' ? strcspn(path, "?#") : 0;

    /* "Invalid IPv6 URL" */
    bool brackets = memchr(hostport, '[', netloc_len) != NULL;
    if (brackets != (memchr(hostport, ']', netloc_len) != NULL)) return OLLAMA_ERROR_REQUEST;

    /* drop the user info */
    const char *hostinfo = hostport;
    for (size_t i = 0; i < netloc_len; i++)
        if (hostport[i] == '@') hostinfo = hostport + i + 1;
    size_t hostinfo_len = netloc_len - (size_t)(hostinfo - hostport);

    /* the checks of _check_bracketed_netloc() and the split of _hostinfo() */
    const char *name;
    size_t name_len;
    const char *port_str = NULL;
    size_t port_len = 0;
    const char *bracket = memchr(hostinfo, '[', hostinfo_len);
    if (bracket) {
        if (bracket != hostinfo) return OLLAMA_ERROR_REQUEST; /* data before '[' */
        name = hostinfo + 1;
        size_t rest = hostinfo_len - 1;
        const char *close = memchr(name, ']', rest);
        name_len = close ? (size_t)(close - name) : rest;
        if (close && close + 1 < name + rest) {
            if (close[1] != ':') return OLLAMA_ERROR_REQUEST; /* data between ']' and ':' */
            port_str = close + 2;
            port_len = rest - name_len - 2;
        }
        if (!valid_bracketed_host(name, name_len)) return OLLAMA_ERROR_REQUEST;
    } else {
        name = hostinfo;
        const char *colon = memchr(hostinfo, ':', hostinfo_len);
        name_len = colon ? (size_t)(colon - hostinfo) : hostinfo_len;
        if (colon) {
            port_str = colon + 1;
            port_len = hostinfo_len - name_len - 1;
        }
        /* brackets only in the user info: Python checks the plain host name instead */
        if (brackets && !valid_bracketed_host(name, name_len)) return OLLAMA_ERROR_REQUEST;
    }

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
    /* like Python's hostname, lowercase up to the first '%': the zone of a scoped
     * IPv6 address is an interface name, which keeps its case */
    const char *percent = memchr(name, '%', name_len);
    size_t lower_len = percent ? (size_t)(percent - name) : name_len;

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
    for (size_t i = 0; i < lower_len; i++) {
        char c = name[i];
        buf_putc(&b, c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c);
    }
    buf_append(&b, name + lower_len, name_len - lower_len);
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
    const char *rest = host;
    long port = DEFAULT_PORT;

    const char *sep = strstr(host, "://");
    if (sep && sep[3] != '\0') {
        scheme = host;
        scheme_len = (size_t)(sep - host);
        rest = sep + 3;
        if (scheme_len == 4 && memcmp(scheme, "http", 4) == 0)
            port = 80;
        else if (scheme_len == 5 && memcmp(scheme, "https", 5) == 0)
            port = 443;
    }

    /* urlsplit() ignores tabs and newlines, such as the CR left by a CRLF .env file */
    char *hostport = malloc(strlen(rest) + 1);
    if (!hostport) return OLLAMA_ERROR_MEMORY;
    size_t n = 0;
    for (const char *p = rest; *p; p++)
        if (*p != '\t' && *p != '\r' && *p != '\n') hostport[n++] = *p;
    hostport[n] = '\0';

    ollama_status_t st = parse_hostport(scheme, scheme_len, port, hostport, out);
    free(hostport);
    return st;
}
