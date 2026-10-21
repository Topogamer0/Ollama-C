/*
 * Search the web through ollama.com (Python: ollama.web_search).
 * Requires an API key: export OLLAMA_API_KEY=...
 */
#include <ollama.h>
#include <stdio.h>

int main(int argc, char **argv) {
    const char *query = argc > 1 ? argv[1] : "what is ollama?";
    ollama_web_search_response_t *res;
    if (ollama_web_search(NULL, query, 3, &res) != OLLAMA_OK) {
        fprintf(stderr, "error: %s\n", ollama_last_error(NULL)->message);
        return 1;
    }
    for (size_t i = 0; i < res->results_count; i++) {
        const ollama_web_search_result_t *r = &res->results[i];
        printf("%s\n%s\n%.300s...\n\n", r->title ? r->title : "", r->url ? r->url : "",
               r->content ? r->content : "");
    }
    ollama_response_free(res);
    ollama_cleanup();
    return 0;
}
