/*
 * Runs a fuzz target on the files given as arguments, without libFuzzer.
 * Used by ctest to replay the corpus and the inputs of past crashes with any
 * compiler, so that fixed bugs stay fixed.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        if (!f) {
            fprintf(stderr, "cannot open %s\n", argv[i]);
            return 1;
        }
        size_t size = 0, cap = 4096;
        uint8_t *data = malloc(cap);
        size_t n;
        while (data && (n = fread(data + size, 1, cap - size, f)) > 0) {
            size += n;
            if (size == cap) {
                uint8_t *bigger = realloc(data, cap * 2);
                if (!bigger) {
                    free(data);
                    data = NULL;
                    break;
                }
                data = bigger;
                cap *= 2;
            }
        }
        fclose(f);
        /* an exact-size copy, like libFuzzer, so that ASan sees reads past the end */
        uint8_t *exact = data ? malloc(size ? size : 1) : NULL;
        if (!exact) {
            free(data);
            fprintf(stderr, "out of memory reading %s\n", argv[i]);
            return 1;
        }
        memcpy(exact, data, size);
        free(data);
        LLVMFuzzerTestOneInput(exact, size);
        free(exact);
    }
    printf("%d inputs replayed\n", argc - 1);
    return 0;
}
