/* Chat with a model (Python: ollama.chat). */
#include <ollama.h>
#include <stdio.h>

int main(void) {
    ollama_message_t messages[] = {
        {.role = "user", .content = "Why is the sky blue?"},
    };
    ollama_chat_request_t req = {.model = "gemma3", .messages = messages, .messages_count = 1};
    ollama_chat_response_t *res;

    if (ollama_chat(NULL, &req, &res) != OLLAMA_OK) {
        fprintf(stderr, "error: %s\n", ollama_last_error(NULL)->message);
        return 1;
    }
    printf("%s\n", res->message.content);

    ollama_response_free(res);
    ollama_cleanup();
    return 0;
}
