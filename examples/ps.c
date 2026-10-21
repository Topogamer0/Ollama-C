/* List the models loaded in memory (Python: ollama.ps). */
#include <ollama.h>
#include <stdio.h>

int main(void) {
    ollama_process_response_t *res;
    if (ollama_ps(NULL, &res) != OLLAMA_OK) {
        fprintf(stderr, "error: %s\n", ollama_last_error(NULL)->message);
        return 1;
    }
    if (res->models_count == 0) printf("No models are running\n");
    for (size_t i = 0; i < res->models_count; i++) {
        const ollama_process_model_t *m = &res->models[i];
        printf("Model: %s\n", m->model);
        printf("  Digest: %s\n", m->digest);
        printf("  Expires at: %s\n", m->expires_at);
        printf("  Size: %lld\n", (long long)m->size);
        printf("  Size VRAM: %lld\n", (long long)m->size_vram);
        printf("  Context length: %lld\n", (long long)m->context_length);
        printf("\n");
    }
    ollama_response_free(res);
    ollama_cleanup();
    return 0;
}
