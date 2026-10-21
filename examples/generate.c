/* Generate a response for a prompt (Python: ollama.generate). */
#include <ollama.h>
#include <stdio.h>

int main(void) {
    ollama_generate_request_t req = {.model = "gemma3", .prompt = "Why is the sky blue?"};
    ollama_generate_response_t *res;

    if (ollama_generate(NULL, &req, &res) != OLLAMA_OK) {
        fprintf(stderr, "error: %s\n", ollama_last_error(NULL)->message);
        return 1;
    }
    printf("%s\n", res->response);

    ollama_response_free(res);
    ollama_cleanup();
    return 0;
}
