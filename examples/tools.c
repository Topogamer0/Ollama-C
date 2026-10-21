/*
 * Tool calling: the model asks to run functions, we run them and send the
 * results back (Python: examples/tools.py).
 */
#include <ollama.h>
#include <stdio.h>
#include <string.h>

static long long add_two_numbers(long long a, long long b) { return a + b; }
static long long subtract_two_numbers(long long a, long long b) { return a - b; }

static ollama_json_t *number_tool(const char *name, const char *description) {
    ollama_json_t *tool = ollama_tool_new(name, description);
    ollama_tool_add_parameter(tool, "a", "integer", "The first number", true);
    ollama_tool_add_parameter(tool, "b", "integer", "The second number", true);
    return tool;
}

int main(void) {
    ollama_client_t *client = ollama_client_new(NULL);
    ollama_json_t *tools = ollama_json_array();
    ollama_json_append(tools, number_tool("add_two_numbers", "Add two numbers"));
    ollama_json_append(tools, number_tool("subtract_two_numbers", "Subtract two numbers"));

    ollama_messages_t *messages = ollama_messages_new();
    ollama_messages_add(messages, "user", "What is three plus one?");
    printf("Prompt: %s\n", messages->items[0].content);

    ollama_chat_request_t req = {.model = "llama3.1", .tools = tools};
    req.messages = messages->items;
    req.messages_count = messages->count;
    ollama_chat_response_t *res;
    if (ollama_chat(client, &req, &res) != OLLAMA_OK) {
        fprintf(stderr, "error: %s\n", ollama_last_error(client)->message);
        return 1;
    }

    if (res->message.tool_calls_count == 0) {
        printf("No tool calls returned from model\n");
    } else {
        ollama_messages_add_message(messages, &res->message);
        for (size_t i = 0; i < res->message.tool_calls_count; i++) {
            const ollama_tool_call_t *call = &res->message.tool_calls[i];
            long long a = ollama_json_as_int(ollama_json_get(call->function.arguments, "a"));
            long long b = ollama_json_as_int(ollama_json_get(call->function.arguments, "b"));
            char *args = ollama_json_stringify(call->function.arguments);
            printf("Calling function: %s\nArguments: %s\n", call->function.name, args);
            ollama_free(args);

            char output[64];
            if (strcmp(call->function.name, "add_two_numbers") == 0)
                snprintf(output, sizeof(output), "%lld", add_two_numbers(a, b));
            else if (strcmp(call->function.name, "subtract_two_numbers") == 0)
                snprintf(output, sizeof(output), "%lld", subtract_two_numbers(a, b));
            else
                snprintf(output, sizeof(output), "unknown function");
            printf("Function output: %s\n", output);

            ollama_message_t result = {.role = "tool", .content = output, .tool_name = call->function.name};
            ollama_messages_add_message(messages, &result);
        }

        /* get the final answer with the results of the tools */
        ollama_response_free(res);
        req.messages = messages->items;
        req.messages_count = messages->count;
        if (ollama_chat(client, &req, &res) != OLLAMA_OK) {
            fprintf(stderr, "error: %s\n", ollama_last_error(client)->message);
            return 1;
        }
        printf("Final response: %s\n", res->message.content);
    }

    ollama_response_free(res);
    ollama_messages_free(messages);
    ollama_json_free(tools);
    ollama_client_free(client);
    return 0;
}
