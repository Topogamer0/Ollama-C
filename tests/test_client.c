/*
 * Integration tests of the public API against tests/mock_server.py, which
 * starts a fake Ollama server and runs this program with OLLAMA_HOST set.
 */
#include "ollama.h"
#include "test.h"

#include <curl/curl.h>
#include <stdlib.h>

static ollama_client_t *client;

static size_t collect(char *ptr, size_t size, size_t nmemb, void *userdata) {
    char **out = userdata;
    size_t old = *out ? strlen(*out) : 0, n = size * nmemb;
    char *s = realloc(*out, old + n + 1);
    if (!s) return 0;
    memcpy(s + old, ptr, n);
    s[old + n] = '\0';
    *out = s;
    return n;
}

/* The last request received by the mock server: {method, path, headers, body}. */
static ollama_json_t *last_request(void) {
    char url[256];
    char *text = NULL;
    snprintf(url, sizeof(url), "%s/_last", ollama_client_host(client));
    CURL *h = curl_easy_init();
    curl_easy_setopt(h, CURLOPT_URL, url);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &text);
    curl_easy_perform(h);
    curl_easy_cleanup(h);
    ollama_json_t *json = ollama_json_parse(text);
    free(text);
    return json;
}

/* The JSON body of the last request. */
static ollama_json_t *last_body(void) {
    ollama_json_t *req = last_request();
    ollama_json_t *body = ollama_json_parse(ollama_json_as_string(ollama_json_get(req, "body")));
    ollama_json_free(req);
    return body;
}

static const char *str_of(const ollama_json_t *o, const char *key) {
    return ollama_json_as_string(ollama_json_get(o, key));
}

static void test_generate(void) {
    ollama_json_t *options = ollama_json_object();
    ollama_json_set_number(options, "temperature", 0);
    ollama_generate_request_t req = {
        .model = "llama3.2",
        .prompt = "Why is the sky blue?",
        .options = options,
        .keep_alive = "5m",
    };
    ollama_generate_response_t *res = NULL;
    CHECK(ollama_generate(client, &req, &res) == OLLAMA_OK);
    CHECK(res != NULL);
    if (res) {
        CHECK_STR(res->response, "Hello, world!");
        CHECK_STR(res->model, "llama3.2");
        CHECK(res->done);
        CHECK_STR(res->done_reason, "stop");
        CHECK(res->context_count == 3 && res->context[2] == 3);
        CHECK(res->total_duration == 1500000000);
        CHECK(res->eval_count == 3);
    }
    ollama_response_free(res);
    CHECK(ollama_last_error(client)->code == OLLAMA_OK);

    ollama_json_t *request = last_request();
    const ollama_json_t *headers = ollama_json_get(request, "headers");
    CHECK_STR(str_of(request, "method"), "POST");
    CHECK_STR(str_of(request, "path"), "/api/generate");
    CHECK_STR(str_of(headers, "content-type"), "application/json");
    CHECK_STR(str_of(headers, "accept"), "application/json");
    CHECK(str_of(headers, "user-agent") && strncmp(str_of(headers, "user-agent"), "ollama-c/", 9) == 0);
    CHECK(ollama_json_get(headers, "authorization") == NULL);
    ollama_json_free(request);

    ollama_json_t *body = last_body();
    CHECK_STR(str_of(body, "model"), "llama3.2");
    CHECK_STR(str_of(body, "prompt"), "Why is the sky blue?");
    CHECK(ollama_json_type(ollama_json_get(body, "stream")) == OLLAMA_JSON_BOOL);
    CHECK(!ollama_json_as_bool(ollama_json_get(body, "stream")));
    CHECK(ollama_json_get(ollama_json_get(body, "options"), "temperature") != NULL);
    CHECK_STR(str_of(body, "keep_alive"), "5m");
    CHECK(ollama_json_get(body, "suffix") == NULL);
    ollama_json_free(body);
    ollama_json_free(options);
}

static void test_generate_stream(void) {
    ollama_generate_request_t req = {.model = "llama3.2", .prompt = "Hi", .keep_alive = "-1"};
    ollama_stream_t *stream = NULL;
    CHECK(ollama_generate_stream(client, &req, &stream) == OLLAMA_OK);
    char text[128] = "";
    int parts = 0;
    bool done = false;
    const ollama_generate_response_t *part;
    while ((part = ollama_stream_next_generate(stream)) != NULL) {
        strncat(text, part->response, sizeof(text) - strlen(text) - 1);
        parts++;
        done = part->done;
        if (done) CHECK(part->context_count == 3);
    }
    CHECK(ollama_stream_error(stream)->code == OLLAMA_OK);
    CHECK_STR(text, "Hello, world!");
    CHECK(parts == 5);
    CHECK(done);
    CHECK(ollama_stream_next_generate(stream) == NULL); /* stays at the end */
    CHECK(ollama_stream_message(stream) == NULL);
    ollama_stream_free(stream);

    ollama_json_t *body = last_body();
    CHECK(ollama_json_as_bool(ollama_json_get(body, "stream")));
    CHECK(ollama_json_type(ollama_json_get(body, "keep_alive")) == OLLAMA_JSON_NUMBER);
    CHECK(ollama_json_as_int(ollama_json_get(body, "keep_alive")) == -1);
    ollama_json_free(body);

    /* a stream can be closed before its end */
    CHECK(ollama_generate_stream(client, &req, &stream) == OLLAMA_OK);
    CHECK(ollama_stream_next_generate(stream) != NULL);
    ollama_stream_free(stream);
}

static void test_errors(void) {
    ollama_generate_request_t missing = {.model = "missing", .prompt = "Hi"};
    ollama_generate_response_t *res = (ollama_generate_response_t *)1;
    CHECK(ollama_generate(client, &missing, &res) == OLLAMA_ERROR_RESPONSE);
    CHECK(res == NULL);
    const ollama_error_t *err = ollama_last_error(client);
    CHECK(err->code == OLLAMA_ERROR_RESPONSE);
    CHECK(err->status_code == 404);
    CHECK_STR(err->message, "model 'missing' not found");

    ollama_generate_request_t plain = {.model = "plain-error"};
    CHECK(ollama_generate(client, &plain, &res) == OLLAMA_ERROR_RESPONSE);
    CHECK(ollama_last_error(client)->status_code == 500);
    CHECK_STR(ollama_last_error(client)->message, "internal failure");

    /* HTTP errors of streams are reported when the stream is opened */
    ollama_stream_t *stream = (ollama_stream_t *)1;
    CHECK(ollama_generate_stream(client, &missing, &stream) == OLLAMA_ERROR_RESPONSE);
    CHECK(stream == NULL);
    CHECK(ollama_last_error(client)->status_code == 404);
    CHECK_STR(ollama_last_error(client)->message, "model 'missing' not found");

    /* errors in the middle of a stream end it */
    ollama_generate_request_t midstream = {.model = "midstream-error"};
    CHECK(ollama_generate_stream(client, &midstream, &stream) == OLLAMA_OK);
    int parts = 0;
    while (ollama_stream_next_generate(stream)) parts++;
    CHECK(parts == 2);
    CHECK(ollama_stream_error(stream)->code == OLLAMA_ERROR_RESPONSE);
    CHECK_STR(ollama_stream_error(stream)->message, "something went wrong");
    CHECK(ollama_stream_next_chat(stream) == NULL);
    ollama_stream_free(stream);

    /* invalid requests never reach the server */
    ollama_generate_request_t no_model = {.prompt = "Hi"};
    CHECK(ollama_generate(client, &no_model, &res) == OLLAMA_ERROR_REQUEST);
    CHECK_STR(ollama_last_error(client)->message, "must provide a model");
    CHECK(ollama_generate(client, NULL, &res) == OLLAMA_ERROR_REQUEST);
    CHECK(ollama_generate(client, &missing, NULL) == OLLAMA_ERROR_REQUEST);
    ollama_image_t images[] = {{.path = "does-not-exist.png"}};
    ollama_generate_request_t bad_image = {.model = "llava", .images = images, .images_count = 1};
    CHECK(ollama_generate(client, &bad_image, &res) == OLLAMA_ERROR_IO);

    /* a successful call clears the previous error */
    ollama_list_response_t *list = NULL;
    CHECK(ollama_list(client, &list) == OLLAMA_OK);
    CHECK(ollama_last_error(client)->code == OLLAMA_OK);
    CHECK_STR(ollama_last_error(client)->message, "");
    ollama_response_free(list);

    /* wrong iterator for the stream type */
    ollama_generate_request_t ok = {.model = "llama3.2"};
    CHECK(ollama_generate_stream(client, &ok, &stream) == OLLAMA_OK);
    CHECK(ollama_stream_next_progress(stream) == NULL);
    CHECK(ollama_stream_error(stream)->code == OLLAMA_ERROR_REQUEST);
    ollama_stream_free(stream);
}

static void test_connection_error(void) {
    ollama_client_t *offline = ollama_client_new("127.0.0.1:1");
    CHECK_STR(ollama_client_host(offline), "http://127.0.0.1:1");
    ollama_list_response_t *list = NULL;
    CHECK(ollama_list(offline, &list) == OLLAMA_ERROR_CONNECTION);
    const char *msg = ollama_last_error(offline)->message;
    CHECK(strncmp(msg, "Failed to connect to Ollama.", 28) == 0);
    ollama_stream_t *stream = NULL;
    ollama_generate_request_t req = {.model = "llama3.2"};
    CHECK(ollama_generate_stream(offline, &req, &stream) == OLLAMA_ERROR_CONNECTION);
    CHECK(stream == NULL);
    ollama_client_free(offline);

    /* hosts that the Python client rejects with a ValueError */
    CHECK(ollama_client_new("[::1") == NULL);
    CHECK(ollama_client_new("example.com:99999") == NULL);
}

static void test_chat(void) {
    ollama_messages_t *history = ollama_messages_new();
    ollama_messages_add(history, "system", "You are a helpful assistant.");
    ollama_messages_add(history, "user", "Why is the sky blue?");

    ollama_chat_request_t req = {.model = "llama3.2", .messages = history->items, .messages_count = history->count};
    ollama_chat_response_t *res = NULL;
    CHECK(ollama_chat(client, &req, &res) == OLLAMA_OK);
    if (res) {
        CHECK_STR(res->message.role, "assistant");
        CHECK_STR(res->message.content, "You said: Why is the sky blue?");
        CHECK(res->message.tool_calls_count == 0);
        CHECK(ollama_messages_add_message(history, &res->message) == OLLAMA_OK);
    }
    ollama_response_free(res);

    ollama_messages_add(history, "user", "Are you sure?");
    req.messages = history->items;
    req.messages_count = history->count;
    CHECK(ollama_chat(client, &req, &res) == OLLAMA_OK);
    if (res) CHECK_STR(res->message.content, "You said: Are you sure?");
    ollama_response_free(res);

    ollama_json_t *body = last_body();
    const ollama_json_t *messages = ollama_json_get(body, "messages");
    CHECK(ollama_json_size(messages) == 4);
    CHECK_STR(str_of(ollama_json_at(messages, 2), "role"), "assistant");
    CHECK_STR(str_of(ollama_json_at(messages, 2), "content"), "You said: Why is the sky blue?");
    CHECK(ollama_json_get(body, "tools") == NULL);
    ollama_json_free(body);
    ollama_messages_free(history);
}

static ollama_json_t *weather_tools(void) {
    ollama_json_t *tool = ollama_tool_new("get_weather", "Get the weather forecast for a city");
    ollama_tool_add_parameter(tool, "city", "string", "Name of the city", true);
    ollama_json_t *days = ollama_tool_add_parameter(tool, "days", "integer", "Number of days", false);
    ollama_json_set_int(days, "minimum", 1);
    ollama_json_t *tools = ollama_json_array();
    ollama_json_append(tools, tool);
    return tools;
}

static void test_chat_tools(void) {
    ollama_json_t *tools = weather_tools();
    ollama_message_t messages[] = {{.role = "user", .content = "Weather in Rome?"}};
    ollama_chat_request_t req = {.model = "llama3.2", .messages = messages, .messages_count = 1, .tools = tools};
    ollama_chat_response_t *res = NULL;
    CHECK(ollama_chat(client, &req, &res) == OLLAMA_OK);
    if (res) {
        CHECK(res->message.tool_calls_count == 1);
        const ollama_tool_call_t *call = &res->message.tool_calls[0];
        CHECK_STR(call->function.name, "get_weather");
        CHECK_STR(str_of(call->function.arguments, "city"), "Rome");
        CHECK(ollama_json_as_int(ollama_json_get(call->function.arguments, "days")) == 2);
    }
    ollama_response_free(res);

    ollama_json_t *body = last_body();
    const ollama_json_t *fn = ollama_json_get(ollama_json_at(ollama_json_get(body, "tools"), 0), "function");
    CHECK_STR(str_of(fn, "name"), "get_weather");
    const ollama_json_t *params = ollama_json_get(fn, "parameters");
    CHECK(ollama_json_size(ollama_json_get(params, "required")) == 1);
    CHECK(ollama_json_as_int(ollama_json_get(ollama_json_get(ollama_json_get(params, "properties"), "days"), "minimum")) ==
          1);
    ollama_json_free(body);
    ollama_json_free(tools);
}

static void test_chat_stream(void) {
    ollama_message_t messages[] = {{.role = "user", .content = "hello there"}};
    ollama_chat_request_t req = {.model = "llama3.2", .messages = messages, .messages_count = 1, .think = "true"};
    ollama_stream_t *stream = NULL;
    CHECK(ollama_chat_stream(client, &req, &stream) == OLLAMA_OK);
    char content[128] = "", thinking[128] = "";
    const ollama_chat_response_t *part;
    while ((part = ollama_stream_next_chat(stream)) != NULL) {
        strncat(content, part->message.content, sizeof(content) - strlen(content) - 1);
        if (part->message.thinking)
            strncat(thinking, part->message.thinking, sizeof(thinking) - strlen(thinking) - 1);
    }
    CHECK(ollama_stream_error(stream)->code == OLLAMA_OK);
    CHECK_STR(content, "You said: hello there");
    CHECK_STR(thinking, "Let me think.");
    const ollama_message_t *full = ollama_stream_message(stream);
    CHECK(full != NULL);
    if (full) {
        CHECK_STR(full->role, "assistant");
        CHECK_STR(full->content, "You said: hello there");
        CHECK_STR(full->thinking, "Let me think.");
    }
    ollama_stream_free(stream);

    ollama_json_t *body = last_body();
    CHECK(ollama_json_type(ollama_json_get(body, "think")) == OLLAMA_JSON_BOOL);
    CHECK(ollama_json_as_bool(ollama_json_get(body, "stream")));
    ollama_json_free(body);

    /* tool calls are collected by the stream too */
    ollama_json_t *tools = weather_tools();
    ollama_chat_request_t tool_req = {.model = "llama3.2", .messages = messages, .messages_count = 1, .tools = tools};
    CHECK(ollama_chat_stream(client, &tool_req, &stream) == OLLAMA_OK);
    while (ollama_stream_next_chat(stream)) {
    }
    full = ollama_stream_message(stream);
    CHECK(full && full->tool_calls_count == 1);
    if (full && full->tool_calls_count == 1) {
        CHECK_STR(full->tool_calls[0].function.name, "get_weather");
        CHECK_STR(str_of(full->tool_calls[0].function.arguments, "city"), "Rome");
        ollama_messages_t *history = ollama_messages_new();
        CHECK(ollama_messages_add_message(history, full) == OLLAMA_OK);
        ollama_messages_free(history);
    }
    ollama_stream_free(stream);
    ollama_json_free(tools);
}

static void test_images(void) {
    ollama_image_t images[] = {{.base64 = "aGVsbG8="}, {.data = "hello", .size = 5}};
    ollama_message_t messages[] = {{.role = "user", .content = "What is this?", .images = images, .images_count = 2}};
    ollama_chat_request_t req = {.model = "llava", .messages = messages, .messages_count = 1};
    ollama_chat_response_t *res = NULL;
    CHECK(ollama_chat(client, &req, &res) == OLLAMA_OK);
    ollama_response_free(res);

    ollama_json_t *body = last_body();
    const ollama_json_t *sent = ollama_json_get(ollama_json_at(ollama_json_get(body, "messages"), 0), "images");
    CHECK(ollama_json_size(sent) == 2);
    CHECK_STR(ollama_json_as_string(ollama_json_at(sent, 0)), "aGVsbG8=");
    CHECK_STR(ollama_json_as_string(ollama_json_at(sent, 1)), "aGVsbG8=");
    ollama_json_free(body);

    FILE *f = fopen("test_image.bin", "wb");
    fputs("hello", f);
    fclose(f);
    ollama_image_t file_image[] = {{.path = "test_image.bin"}};
    ollama_generate_request_t gen = {.model = "llava", .prompt = "Describe", .images = file_image, .images_count = 1};
    ollama_generate_response_t *gres = NULL;
    CHECK(ollama_generate(client, &gen, &gres) == OLLAMA_OK);
    ollama_response_free(gres);
    remove("test_image.bin");
    body = last_body();
    CHECK_STR(ollama_json_as_string(ollama_json_at(ollama_json_get(body, "images"), 0)), "aGVsbG8=");
    ollama_json_free(body);
}

static void test_embed(void) {
    const char *inputs[] = {"The sky is blue", "The grass is green"};
    ollama_embed_request_t req = {.model = "nomic-embed-text", .inputs = inputs, .inputs_count = 2,
                                  .truncate = OLLAMA_FALSE};
    ollama_embed_response_t *res = NULL;
    CHECK(ollama_embed(client, &req, &res) == OLLAMA_OK);
    if (res) {
        CHECK(res->embeddings_count == 2);
        CHECK(res->embeddings[0].size == 3);
        CHECK(res->embeddings[1].values[0] > 0.19 && res->embeddings[1].values[0] < 0.21);
        CHECK(res->embeddings[1].values[2] == -0.3);
    }
    ollama_response_free(res);
    ollama_json_t *body = last_body();
    CHECK(ollama_json_size(ollama_json_get(body, "input")) == 2);
    CHECK(ollama_json_type(ollama_json_get(body, "truncate")) == OLLAMA_JSON_BOOL);
    ollama_json_free(body);

    ollama_embed_request_t single = {.model = "nomic-embed-text", .input = "Hello"};
    CHECK(ollama_embed(client, &single, &res) == OLLAMA_OK);
    if (res) CHECK(res->embeddings_count == 1);
    ollama_response_free(res);
    body = last_body();
    CHECK_STR(str_of(body, "input"), "Hello");
    CHECK(ollama_json_get(body, "truncate") == NULL);
    ollama_json_free(body);

    ollama_embeddings_request_t old = {.model = "nomic-embed-text", .prompt = "Hello"};
    ollama_embeddings_response_t *eres = NULL;
    CHECK(ollama_embeddings(client, &old, &eres) == OLLAMA_OK);
    if (eres) CHECK(eres->embedding_size == 3 && eres->embedding[1] == -0.25);
    ollama_response_free(eres);
}

static void test_models(void) {
    ollama_list_response_t *list = NULL;
    CHECK(ollama_list(client, &list) == OLLAMA_OK);
    if (list) {
        CHECK(list->models_count == 2);
        CHECK_STR(list->models[0].model, "llama3.2:latest");
        CHECK(list->models[0].size == 2019393189);
        CHECK_STR(list->models[0].details.parameter_size, "3.2B");
        CHECK_STR(list->models[1].details.family, "nomic-bert");
        CHECK(list->models[1].details.families_count == 1);
    }
    ollama_response_free(list);

    ollama_show_response_t *show = NULL;
    CHECK(ollama_show(client, "llama3.2", &show) == OLLAMA_OK);
    if (show) {
        CHECK_STR(show->template_, "{{ .Prompt }}");
        CHECK_STR(show->details.quantization_level, "Q4_K_M");
        CHECK(show->capabilities_count == 2);
        CHECK_STR(show->capabilities[1], "tools");
        CHECK_STR(str_of(show->modelinfo, "general.architecture"), "llama");
        CHECK(ollama_json_as_int(ollama_json_get(show->modelinfo, "llama.context_length")) == 131072);
        CHECK(strstr(show->modelfile, "FROM llama3.2") != NULL);
    }
    ollama_response_free(show);
    CHECK(ollama_show(client, "missing", &show) == OLLAMA_ERROR_RESPONSE);

    ollama_process_response_t *ps = NULL;
    CHECK(ollama_ps(client, &ps) == OLLAMA_OK);
    if (ps) {
        CHECK(ps->models_count == 1);
        CHECK(ps->models[0].context_length == 4096);
        CHECK(ps->models[0].size_vram == 3825819519LL);
        CHECK_STR(ps->models[0].expires_at, "2024-01-01T00:05:00Z");
    }
    ollama_response_free(ps);

    CHECK(ollama_copy(client, "llama3.2", "llama3.2-backup") == OLLAMA_OK);
    ollama_json_t *body = last_body();
    CHECK_STR(str_of(body, "destination"), "llama3.2-backup");
    ollama_json_free(body);
    CHECK(ollama_copy(client, "missing", "x") == OLLAMA_ERROR_RESPONSE);

    CHECK(ollama_delete(client, "llama3.2-backup") == OLLAMA_OK);
    ollama_json_t *request = last_request();
    CHECK_STR(str_of(request, "method"), "DELETE");
    ollama_json_free(request);
    CHECK(ollama_delete(client, "missing") == OLLAMA_ERROR_RESPONSE);
    CHECK(ollama_last_error(client)->status_code == 404);
}

static void test_progress(void) {
    ollama_progress_response_t *res = NULL;
    CHECK(ollama_pull(client, "llama3.2", false, &res) == OLLAMA_OK);
    if (res) CHECK_STR(res->status, "success");
    ollama_response_free(res);

    ollama_stream_t *stream = NULL;
    CHECK(ollama_pull_stream(client, "llama3.2", true, &stream) == OLLAMA_OK);
    const ollama_progress_response_t *p;
    int parts = 0;
    int64_t completed = 0;
    char last_status[32] = "";
    while ((p = ollama_stream_next_progress(stream)) != NULL) {
        parts++;
        if (p->total) completed = p->completed;
        /* parts are only valid until the next call, so copy what must be kept */
        if (p->status) snprintf(last_status, sizeof(last_status), "%s", p->status);
    }
    CHECK(parts == 4);
    CHECK(completed == 100);
    CHECK_STR(last_status, "success");
    ollama_stream_free(stream);
    ollama_json_t *body = last_body();
    CHECK(ollama_json_as_bool(ollama_json_get(body, "insecure")));
    ollama_json_free(body);

    CHECK(ollama_push(client, "user/llama3.2", false, &res) == OLLAMA_OK);
    ollama_response_free(res);
    CHECK(ollama_push_stream(client, "user/llama3.2", false, &stream) == OLLAMA_OK);
    ollama_stream_free(stream);

    ollama_json_t *params = ollama_json_object();
    ollama_json_set_number(params, "temperature", 1.5);
    ollama_create_request_t create = {.model = "mario", .from_ = "llama3.2", .system = "You are Mario.",
                                      .parameters = params};
    CHECK(ollama_create(client, &create, &res) == OLLAMA_OK);
    ollama_response_free(res);
    body = last_body();
    CHECK_STR(str_of(body, "from"), "llama3.2");
    CHECK(ollama_json_as_number(ollama_json_get(ollama_json_get(body, "parameters"), "temperature")) == 1.5);
    ollama_json_free(body);
    CHECK(ollama_create_stream(client, &create, &stream) == OLLAMA_OK);
    parts = 0;
    while (ollama_stream_next_progress(stream)) parts++;
    CHECK(parts == 4);
    ollama_stream_free(stream);
    ollama_json_free(params);
}

static void test_blob(void) {
    FILE *f = fopen("test_blob.bin", "wb");
    for (int i = 0; i < 100000; i++) fputc(i % 251, f);
    fclose(f);
    char *digest = NULL;
    CHECK(ollama_create_blob(client, "test_blob.bin", &digest) == OLLAMA_OK);
    CHECK(digest && strncmp(digest, "sha256:", 7) == 0 && strlen(digest) == 71);
    ollama_json_t *request = last_request();
    CHECK(strstr(str_of(request, "path"), digest ? digest : "?") != NULL);
    ollama_json_free(request);
    ollama_free(digest);
    remove("test_blob.bin");

    CHECK(ollama_create_blob(client, "does-not-exist.bin", &digest) == OLLAMA_ERROR_IO);
    CHECK(digest == NULL);
}

static void test_headers_and_web(void) {
    ollama_client_t *c = ollama_client_new(ollama_client_host(client));
    ollama_web_search_response_t *search = NULL;
    CHECK(ollama_web_search(c, "ollama", 0, &search) == OLLAMA_ERROR_REQUEST);
    CHECK(strstr(ollama_last_error(c)->message, "Bearer") != NULL);
    ollama_web_fetch_response_t *fetch = NULL;
    CHECK(ollama_web_fetch(c, "https://ollama.com", &fetch) == OLLAMA_ERROR_REQUEST);

    CHECK(ollama_client_set_header(c, "Authorization", "Bearer secret") == OLLAMA_OK);
    CHECK(ollama_client_set_header(c, "X-Custom", "one") == OLLAMA_OK);
    CHECK(ollama_client_set_header(c, "x-custom", "two") == OLLAMA_OK);
    CHECK(ollama_client_set_header(c, "User-Agent", "ignored") == OLLAMA_OK);
    CHECK(ollama_client_set_header(c, "Bad:Name", "x") == OLLAMA_ERROR_REQUEST);
    ollama_client_set_timeout(c, 5000);
    ollama_list_response_t *list = NULL;
    CHECK(ollama_list(c, &list) == OLLAMA_OK);
    ollama_response_free(list);
    ollama_json_t *request = last_request();
    const ollama_json_t *headers = ollama_json_get(request, "headers");
    CHECK_STR(str_of(headers, "authorization"), "Bearer secret");
    CHECK_STR(str_of(headers, "x-custom"), "two");
    CHECK(strncmp(str_of(headers, "user-agent"), "ollama-c/", 9) == 0);
    ollama_json_free(request);

    CHECK(ollama_client_set_header(c, "X-Custom", NULL) == OLLAMA_OK);
    CHECK(ollama_list(c, &list) == OLLAMA_OK);
    ollama_response_free(list);
    request = last_request();
    CHECK(ollama_json_get(ollama_json_get(request, "headers"), "x-custom") == NULL);
    ollama_json_free(request);
    ollama_client_free(c);
}

static void test_systemone(void) {
    ollama_json_t *state = ollama_json_string("Why is the sky blue?");
    ollama_json_t *questions = ollama_json_parse(
        "{\"is_question\":{\"type\":\"noul\",\"instructions\":\"Is this a question?\"},"
        "\"topic\":{\"type\":\"choice\",\"instructions\":\"Topic?\",\"criteria\":{\"science\":null,\"art\":null}}}");
    ollama_systemone_request_t req = {.model = "systemone", .state = state, .questions = questions};
    ollama_systemone_response_t *res = NULL;
    CHECK(ollama_systemone(client, &req, &res) == OLLAMA_OK);
    if (res) {
        CHECK(res->answers_count == 2);
        CHECK_STR(res->answers[0].name, "is_question");
        CHECK(res->answers[0].noul == 0.9);
        CHECK_STR(res->answers[1].choice, "science");
        CHECK(ollama_json_as_number(ollama_json_get(res->answers[1].probabilities, "art")) == 0.2);
        CHECK(res->usage.input_tokens == 42);
    }
    ollama_response_free(res);
    ollama_json_free(state);
    ollama_json_free(questions);
}

static void test_default_client(void) {
    /* NULL uses a client configured from OLLAMA_HOST, like ollama.list() */
    ollama_list_response_t *list = NULL;
    CHECK(ollama_list(NULL, &list) == OLLAMA_OK);
    if (list) CHECK(list->models_count == 2);
    ollama_response_free(list);
    CHECK_STR(ollama_client_host(NULL), ollama_client_host(client));
    CHECK(ollama_last_error(NULL)->code == OLLAMA_OK);
    ollama_cleanup();
    ollama_cleanup();
}

int main(void) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    if (!getenv("OLLAMA_HOST")) {
        fprintf(stderr, "run this test through tests/mock_server.py\n");
        return 1;
    }
    client = ollama_client_new(NULL);
    CHECK(client != NULL);
    printf("ollama-c %s, server %s\n", ollama_version(), ollama_client_host(client));

    RUN(test_generate);
    RUN(test_generate_stream);
    RUN(test_errors);
    RUN(test_connection_error);
    RUN(test_chat);
    RUN(test_chat_tools);
    RUN(test_chat_stream);
    RUN(test_images);
    RUN(test_embed);
    RUN(test_models);
    RUN(test_progress);
    RUN(test_blob);
    RUN(test_headers_and_web);
    RUN(test_systemone);
    RUN(test_default_client);

    ollama_client_free(client);
    curl_global_cleanup();
    return test_report();
}
