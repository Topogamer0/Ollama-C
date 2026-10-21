/*
 * Fuzz target for the OLLAMA_HOST parser. Normalizing a host must never fail
 * on valid memory and must be idempotent: parsing a normalized URL gives the
 * same URL back. Like urlsplit(), it drops tabs and newlines after the scheme.
 * tests/host_parity.py compares the results with the Python client.
 */
#include "internal.h"

#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    char *host = ol_strndup((const char *)data, size); /* stops at the first NUL, like getenv() */
    if (!host) return 0;
    char *url;
    if (parse_host(host, &url) == OLLAMA_OK) {
        char *again;
        const char *rest = strstr(url, "://");
        if (!rest || strpbrk(rest, "\t\r\n")) abort();
        if (parse_host(url, &again) != OLLAMA_OK || strcmp(url, again) != 0) abort();
        free(again);
    }
    free(url);
    free(host);
    return 0;
}
