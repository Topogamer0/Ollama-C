/* Ask a vision model about an image (Python: examples/multimodal-chat.py). */
#include <ollama.h>
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <image>\n", argv[0]);
        return 2;
    }
    ollama_image_t images[] = {{.path = argv[1]}};
    ollama_message_t messages[] = {{
        .role = "user",
        .content = "What is in this image? Be concise.",
        .images = images,
        .images_count = 1,
    }};
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
