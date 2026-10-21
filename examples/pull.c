/* Download a model showing the progress (Python: examples/pull.py). */
#include <ollama.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *model = argc > 1 ? argv[1] : "gemma3";
    ollama_stream_t *stream;
    if (ollama_pull_stream(NULL, model, false, &stream) != OLLAMA_OK) {
        fprintf(stderr, "error: %s\n", ollama_last_error(NULL)->message);
        return 1;
    }

    char current[128] = "";
    const ollama_progress_response_t *p;
    while ((p = ollama_stream_next_progress(stream)) != NULL) {
        if (p->digest && p->total > 0) {
            if (strcmp(current, p->digest) != 0) {
                if (*current) putchar('\n');
                snprintf(current, sizeof(current), "%s", p->digest);
            }
            printf("\rpulling %.19s: %3d%% (%lld/%lld MB)", p->digest, (int)(100 * p->completed / p->total),
                   (long long)(p->completed >> 20), (long long)(p->total >> 20));
            fflush(stdout);
        } else if (p->status) {
            if (*current) {
                putchar('\n');
                current[0] = '\0';
            }
            printf("%s\n", p->status);
        }
    }

    int failed = ollama_stream_error(stream)->code != OLLAMA_OK;
    if (failed) fprintf(stderr, "\nerror: %s\n", ollama_stream_error(stream)->message);
    ollama_stream_free(stream);
    ollama_cleanup();
    return failed;
}
