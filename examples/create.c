/* Create a custom model (Python: examples/create.py). */
#include <ollama.h>
#include <stdio.h>

int main(void) {
    ollama_create_request_t req = {
        .model = "my-assistant",
        .from_ = "gemma3",
        .system = "You are Mario from Super Mario Bros.",
    };
    ollama_stream_t *stream;
    if (ollama_create_stream(NULL, &req, &stream) != OLLAMA_OK) {
        fprintf(stderr, "error: %s\n", ollama_last_error(NULL)->message);
        return 1;
    }
    const ollama_progress_response_t *p;
    while ((p = ollama_stream_next_progress(stream)) != NULL) printf("%s\n", p->status ? p->status : "");

    int failed = ollama_stream_error(stream)->code != OLLAMA_OK;
    if (failed) fprintf(stderr, "error: %s\n", ollama_stream_error(stream)->message);
    ollama_stream_free(stream);
    ollama_cleanup();
    return failed;
}
