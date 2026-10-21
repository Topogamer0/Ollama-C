/* Stream a chat response (Python: ollama.chat(..., stream=True)). */
#include <ollama.h>
#include <stdio.h>

int main(void) {
    ollama_message_t messages[] = {
        {.role = "user", .content = "Why is the sky blue?"},
    };
    ollama_chat_request_t req = {.model = "gemma3", .messages = messages, .messages_count = 1};
    ollama_stream_t *stream;

    if (ollama_chat_stream(NULL, &req, &stream) != OLLAMA_OK) {
        fprintf(stderr, "error: %s\n", ollama_last_error(NULL)->message);
        return 1;
    }
    const ollama_chat_response_t *part;
    while ((part = ollama_stream_next_chat(stream)) != NULL) {
        fputs(part->message.content, stdout);
        fflush(stdout);
    }
    putchar('\n');

    int failed = ollama_stream_error(stream)->code != OLLAMA_OK;
    if (failed) fprintf(stderr, "error: %s\n", ollama_stream_error(stream)->message);
    ollama_stream_free(stream);
    ollama_cleanup();
    return failed;
}
