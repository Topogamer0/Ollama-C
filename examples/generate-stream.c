/* Stream a generated response (Python: ollama.generate(..., stream=True)). */
#include <ollama.h>
#include <stdio.h>

int main(void) {
    ollama_generate_request_t req = {.model = "gemma3", .prompt = "Why is the sky blue?"};
    ollama_stream_t *stream;

    if (ollama_generate_stream(NULL, &req, &stream) != OLLAMA_OK) {
        fprintf(stderr, "error: %s\n", ollama_last_error(NULL)->message);
        return 1;
    }
    const ollama_generate_response_t *part;
    while ((part = ollama_stream_next_generate(stream)) != NULL) {
        fputs(part->response, stdout);
        fflush(stdout);
    }
    putchar('\n');

    int failed = ollama_stream_error(stream)->code != OLLAMA_OK;
    if (failed) fprintf(stderr, "error: %s\n", ollama_stream_error(stream)->message);
    ollama_stream_free(stream);
    ollama_cleanup();
    return failed;
}
