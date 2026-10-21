/* List the local models (Python: ollama.list). */
#include <ollama.h>
#include <stdio.h>

int main(void) {
    ollama_list_response_t *res;
    if (ollama_list(NULL, &res) != OLLAMA_OK) {
        fprintf(stderr, "error: %s\n", ollama_last_error(NULL)->message);
        return 1;
    }
    for (size_t i = 0; i < res->models_count; i++) {
        const ollama_list_model_t *m = &res->models[i];
        printf("Name: %s\n", m->model);
        printf("  Size (MB): %.2f\n", (double)m->size / 1024 / 1024);
        if (m->details.format) {
            printf("  Format: %s\n", m->details.format);
            printf("  Family: %s\n", m->details.family);
            printf("  Parameter Size: %s\n", m->details.parameter_size);
            printf("  Quantization Level: %s\n", m->details.quantization_level);
        }
        printf("\n");
    }
    ollama_response_free(res);
    ollama_cleanup();
    return 0;
}
