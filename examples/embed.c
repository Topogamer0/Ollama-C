/* Compute embeddings (Python: ollama.embed). */
#include <ollama.h>
#include <stdio.h>

int main(void) {
    const char *inputs[] = {"The sky is blue because of Rayleigh scattering", "Grass is green"};
    ollama_embed_request_t req = {.model = "embeddinggemma", .inputs = inputs, .inputs_count = 2};
    ollama_embed_response_t *res;

    if (ollama_embed(NULL, &req, &res) != OLLAMA_OK) {
        fprintf(stderr, "error: %s\n", ollama_last_error(NULL)->message);
        return 1;
    }
    for (size_t i = 0; i < res->embeddings_count; i++) {
        const ollama_embedding_t *e = &res->embeddings[i];
        printf("\"%s\": %zu dimensions [%f, %f, %f, ...]\n", inputs[i], e->size, e->values[0], e->values[1],
               e->values[2]);
    }

    ollama_response_free(res);
    ollama_cleanup();
    return 0;
}
