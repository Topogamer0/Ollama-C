/* An interactive chat that keeps the conversation history. */
#include <ollama.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    ollama_client_t *client = ollama_client_new(NULL);
    ollama_messages_t *history = ollama_messages_new();
    char line[4096];

    ollama_messages_add(history, "system", "You are a helpful assistant. Keep your answers short.");
    printf("Chat with gemma3, an empty line quits.\n");

    for (;;) {
        printf("\n>>> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        line[strcspn(line, "\n")] = '\0';
        if (!*line) break;
        ollama_messages_add(history, "user", line);

        ollama_chat_request_t req = {
            .model = "gemma3",
            .messages = history->items,
            .messages_count = history->count,
        };
        ollama_stream_t *stream;
        if (ollama_chat_stream(client, &req, &stream) != OLLAMA_OK) {
            fprintf(stderr, "error: %s\n", ollama_last_error(client)->message);
            break;
        }
        const ollama_chat_response_t *part;
        while ((part = ollama_stream_next_chat(stream)) != NULL) {
            fputs(part->message.content, stdout);
            fflush(stdout);
        }
        putchar('\n');
        if (ollama_stream_error(stream)->code != OLLAMA_OK)
            fprintf(stderr, "error: %s\n", ollama_stream_error(stream)->message);
        else
            /* the stream assembles the whole answer for us */
            ollama_messages_add_message(history, ollama_stream_message(stream));
        ollama_stream_free(stream);
    }

    ollama_messages_free(history);
    ollama_client_free(client);
    return 0;
}
