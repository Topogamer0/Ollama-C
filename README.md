# Ollama-C

A C library for [Ollama](https://ollama.com): a port of the official Python library,
[ollama-python](https://github.com/ollama/ollama-python), with the same functions, parameters and
behavior, written in portable C99 on top of libcurl.

```c
#include <ollama.h>
#include <stdio.h>

int main(void) {
    ollama_message_t messages[] = {{.role = "user", .content = "Why is the sky blue?"}};
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
```

## Features

- Every method of the Python `Client`: `generate`, `chat`, `embed`, `embeddings`, `pull`, `push`,
  `create`, `create_blob`, `list`, `show`, `ps`, `copy`, `delete`, `web_search`, `web_fetch`, `systemone`
- Streaming (`stream=True`) through iterators
- Tool calling, thinking models, structured outputs (JSON schemas), images, logprobs
- Same configuration as Python: `OLLAMA_HOST` (with the same host parsing rules), `OLLAMA_API_KEY`,
  custom headers, timeout
- Typed responses, plus the full JSON of every response
- A small JSON API for options, schemas, tool definitions and tool arguments
- No dependencies besides libcurl; works from C and C++

## Building

Requirements: a C99 compiler, CMake 3.14+, libcurl with its development headers
(`libcurl4-openssl-dev` on Debian/Ubuntu, `libcurl-devel` on Fedora, `curl` on Homebrew).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build            # optional: runs the tests (needs Python 3 for the fake server)
sudo cmake --install build        # installs libollama, ollama.h, CMake and pkg-config files
```

Options: `-DBUILD_SHARED_LIBS=ON` for a shared library, `-DOLLAMA_BUILD_EXAMPLES=OFF`,
`-DOLLAMA_BUILD_TESTS=OFF`.

Using the library in your project:

```cmake
# after installing
find_package(ollama-c REQUIRED)
target_link_libraries(my_app PRIVATE ollama::ollama)

# or without installing
add_subdirectory(Ollama-C)
target_link_libraries(my_app PRIVATE ollama::ollama)
```

```sh
cc my_app.c $(pkg-config --cflags --libs --static ollama-c)
```

## From Python to C

| Python | C |
| --- | --- |
| `ollama.generate(model='gemma3', prompt='Hi')` | `ollama_generate(NULL, &(ollama_generate_request_t){.model = "gemma3", .prompt = "Hi"}, &res)` |
| `ollama.chat(model=..., messages=[...])` | `ollama_chat(client, &req, &res)` |
| `ollama.chat(..., stream=True)` | `ollama_chat_stream(client, &req, &stream)` |
| `ollama.embed(model=..., input=[...])` | `ollama_embed(client, &req, &res)` |
| `ollama.pull('gemma3')` | `ollama_pull(client, "gemma3", false, &res)` |
| `ollama.pull('gemma3', stream=True)` | `ollama_pull_stream(client, "gemma3", false, &stream)` |
| `ollama.create(model=..., from_=..., system=...)` | `ollama_create(client, &req, &res)` |
| `ollama.list()` / `ollama.ps()` | `ollama_list(client, &res)` / `ollama_ps(client, &res)` |
| `ollama.show('gemma3')` | `ollama_show(client, "gemma3", &res)` |
| `ollama.copy(src, dst)` / `ollama.delete(m)` | `ollama_copy(client, src, dst)` / `ollama_delete(client, m)` |
| `client.create_blob(path)` | `ollama_create_blob(client, path, &digest)` |
| `ollama.web_search(query)` / `web_fetch(url)` | `ollama_web_search(client, query, 3, &res)` / `ollama_web_fetch(client, url, &res)` |
| `Client(host='http://localhost:11434', headers={...})` | `ollama_client_new("http://localhost:11434")` + `ollama_client_set_header()` |
| `ResponseError` (`e.error`, `e.status_code`) | `OLLAMA_ERROR_RESPONSE` + `ollama_last_error(client)->message / status_code` |
| `options={'temperature': 0}` | `.options = json` with `ollama_json_set_number(json, "temperature", 0)` |
| `think=True` / `think='high'` | `.think = "true"` / `.think = "high"` |
| `keep_alive='5m'` / `keep_alive=-1` | `.keep_alive = "5m"` / `.keep_alive = "-1"` |
| `format='json'` / `format=Model.model_json_schema()` | `.format = ollama_json_string("json")` / `.format = ollama_json_parse(schema)` |
| `tools=[my_function]` | `.tools = ollama_tool_new(...)` + `ollama_tool_add_parameter(...)` |
| `response.message.content` | `res->message.content` |
| `response['eval_count']` | `res->eval_count`, or any field through `res->json` |

Request structs use designated initializers, so you only write the parameters you would pass as
keyword arguments in Python; everything left at zero or `NULL` is not sent.

Passing `NULL` as the client uses a default client configured from the environment, like the
module-level functions `ollama.chat(...)`; `ollama_cleanup()` releases it.

## Usage

### Streaming

```c
ollama_stream_t *stream;
if (ollama_chat_stream(client, &req, &stream) == OLLAMA_OK) {
    const ollama_chat_response_t *part;
    while ((part = ollama_stream_next_chat(stream)) != NULL)
        fputs(part->message.content, stdout);
    if (ollama_stream_error(stream)->code != OLLAMA_OK)
        fprintf(stderr, "error: %s\n", ollama_stream_error(stream)->message);
    ollama_stream_free(stream);
}
```

There is an iterator per kind of stream: `ollama_stream_next_generate`, `ollama_stream_next_chat`
and `ollama_stream_next_progress` (pull, push, create). A part is valid until the next call.
HTTP errors such as "model not found" are returned by the `*_stream` function itself.
For chat streams, `ollama_stream_message(stream)` returns the whole message assembled from the
parts (content, thinking and tool calls), ready to be added to the history.

### Custom client

```c
ollama_client_t *client = ollama_client_new("http://localhost:11434");
ollama_client_set_header(client, "x-some-header", "some-value");
ollama_client_set_timeout(client, 30000); /* ms; like httpx, limits connecting and waiting for data */
/* ... */
ollama_client_free(client);
```

`ollama_client_new(NULL)` reads `OLLAMA_HOST` and accepts the same values as the Python client
(`0.0.0.0`, `:8080`, `example.com/path`, `https://[::1]:11434`, ...). When `OLLAMA_API_KEY` is set
it is sent as `Authorization: Bearer ...`, which is needed for `ollama_web_search`,
`ollama_web_fetch` and for using ollama.com as host.

### Chat history

```c
ollama_messages_t *history = ollama_messages_new();
ollama_messages_add(history, "user", "Why is the sky blue?");

ollama_chat_request_t req = {.model = "gemma3", .messages = history->items, .messages_count = history->count};
ollama_chat(client, &req, &res);
ollama_messages_add_message(history, &res->message); /* deep copy: the response can be freed */
ollama_response_free(res);
/* ... */
ollama_messages_free(history);
```

### Tools

```c
ollama_json_t *tool = ollama_tool_new("get_weather", "Get the current weather for a city");
ollama_tool_add_parameter(tool, "city", "string", "The name of the city", true);
ollama_json_t *tools = ollama_json_array();
ollama_json_append(tools, tool);

ollama_chat_request_t req = {.model = "llama3.1", .messages = msgs, .messages_count = n, .tools = tools};
ollama_chat(client, &req, &res);
for (size_t i = 0; i < res->message.tool_calls_count; i++) {
    const ollama_tool_call_t *call = &res->message.tool_calls[i];
    const char *city = ollama_json_as_string(ollama_json_get(call->function.arguments, "city"));
    /* run the tool, then send a {.role = "tool", .tool_name = ..., .content = result} message */
}
```

Tools can also be written as JSON with `ollama_json_parse()`. See
[`examples/tools.c`](examples/tools.c) for the full round trip.

### Structured outputs, images, embeddings

```c
ollama_json_t *schema = ollama_json_parse("{\"type\":\"object\",\"properties\":{\"age\":{\"type\":\"integer\"}}}");
ollama_chat_request_t req = {.model = "gemma3", .messages = msgs, .messages_count = 1, .format = schema};

ollama_image_t images[] = {{.path = "cat.jpg"}};   /* or {.data = bytes, .size = n} or {.base64 = "..."} */
ollama_message_t msg = {.role = "user", .content = "What is this?", .images = images, .images_count = 1};

const char *inputs[] = {"first text", "second text"};
ollama_embed_request_t ereq = {.model = "embeddinggemma", .inputs = inputs, .inputs_count = 2};
ollama_embed(client, &ereq, &eres); /* eres->embeddings[i].values / .size */
```

### Errors

Every function returns `OLLAMA_OK` or an error code:

| Code | Python equivalent |
| --- | --- |
| `OLLAMA_ERROR_REQUEST` | `RequestError` / `ValueError` (e.g. missing model) |
| `OLLAMA_ERROR_RESPONSE` | `ResponseError`, with `status_code` |
| `OLLAMA_ERROR_CONNECTION` | `ConnectionError` ("Failed to connect to Ollama...") |
| `OLLAMA_ERROR_TRANSPORT` | other httpx errors (timeouts, TLS, ...) |
| `OLLAMA_ERROR_PARSE`, `OLLAMA_ERROR_IO`, `OLLAMA_ERROR_MEMORY` | invalid JSON, unreadable file, out of memory |

`ollama_last_error(client)` gives the message and HTTP status code of the last call.

### Memory

- Requests only borrow the memory they point to.
- Responses belong to the caller: release them with `ollama_response_free()`. All their strings,
  arrays and JSON values live until then.
- Stream parts belong to the stream.
- JSON values you create are released with `ollama_json_free()`; strings returned by the library
  (`ollama_json_stringify`, `ollama_create_blob`, base64 helpers) with `ollama_free()`.

### Threads

A client must not be used by several threads at the same time: create one client per thread.
The default client (`NULL`) is not thread-safe. In multi-threaded programs call
`curl_global_init(CURL_GLOBAL_DEFAULT)` once at startup.

## Differences from the Python library

- There is no `AsyncClient`: run blocking calls on your own threads, with one client per thread.
- Python functions cannot be passed as tools: use `ollama_tool_new()` or a JSON schema.
- Options, formats and other free-form dicts are `ollama_json_t` values.
- `think` and `keep_alive` are strings (`"true"`, `"high"`, `"5m"`, `"-1"`).
- Chat streams can assemble the full message for you (`ollama_stream_message`).

## Examples

The [`examples`](examples) directory mirrors the examples of ollama-python and is built with the
library (`build/examples/`): `generate`, `generate-stream`, `chat`, `chat-stream`,
`chat-with-history`, `tools`, `structured-outputs`, `thinking`, `multimodal`, `embed`, `list`,
`show`, `ps`, `pull`, `create`, `web-search`.

```sh
ollama pull gemma3
./build/examples/chat-stream
```

## Tests

`ctest --test-dir build` runs the unit tests and the integration tests, which drive the public API
against a fake Ollama server ([`tests/mock_server.py`](tests/mock_server.py)) and check both the
requests sent and the parsing of the answers.
