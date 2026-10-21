/* Show the reasoning of a thinking model (Python: examples/thinking.py). */
#include <ollama.h>
#include <stdio.h>

int main(void) {
    ollama_message_t messages[] = {
        {.role = "user", .content = "What is 10 + 23?"},
    };
    ollama_chat_request_t req = {
        .model = "deepseek-r1",
        .messages = messages,
        .messages_count = 1,
        .think = "true",
    };
    ollama_chat_response_t *res;
    if (ollama_chat(NULL, &req, &res) != OLLAMA_OK) {
        fprintf(stderr, "error: %s\n", ollama_last_error(NULL)->message);
        return 1;
    }
    printf("Thinking:\n========\n\n%s\n", res->message.thinking ? res->message.thinking : "");
    printf("\nResponse:\n========\n\n%s\n", res->message.content);

    ollama_response_free(res);
    ollama_cleanup();
    return 0;
}
