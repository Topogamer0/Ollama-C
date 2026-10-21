/* Constrain the answer to a JSON schema (Python: examples/structured-outputs.py). */
#include <ollama.h>
#include <stdio.h>

static const char *SCHEMA =
    "{\"type\": \"object\", \"properties\": {"
    "  \"friends\": {\"type\": \"array\", \"items\": {\"type\": \"object\", \"properties\": {"
    "    \"name\": {\"type\": \"string\"},"
    "    \"age\": {\"type\": \"integer\"},"
    "    \"is_available\": {\"type\": \"boolean\"}},"
    "  \"required\": [\"name\", \"age\", \"is_available\"]}}},"
    " \"required\": [\"friends\"]}";

int main(void) {
    ollama_json_t *format = ollama_json_parse(SCHEMA);
    ollama_json_t *options = ollama_json_object();
    ollama_json_set_number(options, "temperature", 0); /* more deterministic output */

    ollama_message_t messages[] = {{
        .role = "user",
        .content = "I have two friends. The first is Ollama 22 years old busy saving the world, "
                   "and the second is Alonso 23 years old and wants to hang out. "
                   "Return a list of friends in JSON format",
    }};
    ollama_chat_request_t req = {
        .model = "llama3.1:8b",
        .messages = messages,
        .messages_count = 1,
        .format = format,
        .options = options,
    };
    ollama_chat_response_t *res;
    if (ollama_chat(NULL, &req, &res) != OLLAMA_OK) {
        fprintf(stderr, "error: %s\n", ollama_last_error(NULL)->message);
        return 1;
    }

    ollama_json_t *answer = ollama_json_parse(res->message.content);
    const ollama_json_t *friends = ollama_json_get(answer, "friends");
    for (size_t i = 0; i < ollama_json_size(friends); i++) {
        const ollama_json_t *f = ollama_json_at(friends, i);
        printf("%s, %lld years old, %s\n", ollama_json_as_string(ollama_json_get(f, "name")),
               (long long)ollama_json_as_int(ollama_json_get(f, "age")),
               ollama_json_as_bool(ollama_json_get(f, "is_available")) ? "available" : "busy");
    }

    ollama_json_free(answer);
    ollama_response_free(res);
    ollama_json_free(format);
    ollama_json_free(options);
    ollama_cleanup();
    return 0;
}
