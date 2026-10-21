/*
 * Reads hosts encoded in hex, one per line, and prints for each one "OK <url
 * in hex>" or "ERR", for tests/host_parity.py.
 */
#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
}

int main(void) {
    char line[8192];
    while (fgets(line, sizeof(line), stdin)) {
        size_t n = strcspn(line, "\r\n");
        char host[4096];
        size_t k = 0;
        for (size_t i = 0; i + 1 < n && k + 1 < sizeof(host); i += 2)
            host[k++] = (char)(hex_value(line[i]) * 16 + hex_value(line[i + 1]));
        host[k] = '\0';
        char *url = NULL;
        if (parse_host(host, &url) == OLLAMA_OK) {
            printf("OK ");
            for (const char *p = url; *p; p++) printf("%02x", (unsigned char)*p);
            printf("\n");
        } else {
            printf("ERR\n");
        }
        free(url);
    }
    return 0;
}
