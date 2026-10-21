/* Show information about a model (Python: ollama.show). */
#include <ollama.h>
#include <stdio.h>

int main(int argc, char **argv) {
    const char *model = argc > 1 ? argv[1] : "gemma3";
    ollama_show_response_t *res;
    if (ollama_show(NULL, model, &res) != OLLAMA_OK) {
        fprintf(stderr, "error: %s\n", ollama_last_error(NULL)->message);
        return 1;
    }
    printf("Model: %s\n", model);
    printf("Family: %s\n", res->details.family ? res->details.family : "-");
    printf("Parameters: %s\n", res->details.parameter_size ? res->details.parameter_size : "-");
    printf("Quantization: %s\n", res->details.quantization_level ? res->details.quantization_level : "-");
    printf("Capabilities:");
    for (size_t i = 0; i < res->capabilities_count; i++) printf(" %s", res->capabilities[i]);
    printf("\n\nModel info:\n");
    for (size_t i = 0; i < ollama_json_size(res->modelinfo); i++) {
        char *value = ollama_json_stringify(ollama_json_at(res->modelinfo, i));
        printf("  %s: %.80s\n", ollama_json_key_at(res->modelinfo, i), value);
        ollama_free(value);
    }
    ollama_response_free(res);
    ollama_cleanup();
    return 0;
}
