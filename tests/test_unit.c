/* Unit tests of the internals: host parsing, JSON, base64, SHA-256, serialization, parsing. */
#include "internal.h"
#include "test.h"

#include <locale.h>
#include <stdlib.h>

/* ---- host: the doctests of _parse_host() in the Python client ---- */

static void check_host(const char *input, const char *expected) {
    char *out;
    CHECK(parse_host(input, &out) == OLLAMA_OK);
    CHECK_STR(out, expected);
    free(out);
}

/* hosts for which urlsplit() raises ValueError in the Python client */
static void check_invalid_host(const char *input) {
    char *out = (char *)1;
    if (parse_host(input, &out) != OLLAMA_ERROR_REQUEST) fprintf(stderr, "accepted invalid host: %s\n", input);
    CHECK(out == NULL);
}

static void test_parse_host(void) {
    check_host(NULL, "http://127.0.0.1:11434");
    check_host("", "http://127.0.0.1:11434");
    check_host("1.2.3.4", "http://1.2.3.4:11434");
    check_host(":56789", "http://127.0.0.1:56789");
    check_host("1.2.3.4:56789", "http://1.2.3.4:56789");
    check_host("http://1.2.3.4", "http://1.2.3.4:80");
    check_host("https://1.2.3.4", "https://1.2.3.4:443");
    check_host("https://1.2.3.4:56789", "https://1.2.3.4:56789");
    check_host("example.com", "http://example.com:11434");
    check_host("example.com:56789", "http://example.com:56789");
    check_host("http://example.com", "http://example.com:80");
    check_host("https://example.com", "https://example.com:443");
    check_host("https://example.com:56789", "https://example.com:56789");
    check_host("example.com/", "http://example.com:11434");
    check_host("example.com:56789/", "http://example.com:56789");
    check_host("example.com/path", "http://example.com:11434/path");
    check_host("example.com:56789/path", "http://example.com:56789/path");
    check_host("https://example.com:56789/path", "https://example.com:56789/path");
    check_host("example.com:56789/path/", "http://example.com:56789/path");
    check_host("[0001:002:003:0004::1]", "http://[0001:002:003:0004::1]:11434");
    check_host("[0001:002:003:0004::1]:56789", "http://[0001:002:003:0004::1]:56789");
    check_host("http://[0001:002:003:0004::1]", "http://[0001:002:003:0004::1]:80");
    check_host("https://[0001:002:003:0004::1]", "https://[0001:002:003:0004::1]:443");
    check_host("https://[0001:002:003:0004::1]:56789", "https://[0001:002:003:0004::1]:56789");
    check_host("[0001:002:003:0004::1]/", "http://[0001:002:003:0004::1]:11434");
    check_host("[0001:002:003:0004::1]:56789/", "http://[0001:002:003:0004::1]:56789");
    check_host("[0001:002:003:0004::1]/path", "http://[0001:002:003:0004::1]:11434/path");
    check_host("[0001:002:003:0004::1]:56789/path", "http://[0001:002:003:0004::1]:56789/path");
    check_host("https://[0001:002:003:0004::1]:56789/path", "https://[0001:002:003:0004::1]:56789/path");
    check_host("[0001:002:003:0004::1]:56789/path/", "http://[0001:002:003:0004::1]:56789/path");
    /* beyond the doctests */
    check_host("0.0.0.0", "http://0.0.0.0:11434");
    check_host("Example.COM", "http://example.com:11434");
    check_host("user:pass@example.com:1234", "http://example.com:1234");
    check_host("example.com:1234?x=1", "http://example.com:1234");
    check_host("example.com:", "http://example.com:11434");
    check_host("example.com:0", "http://example.com:11434");
    check_host("example.com:000080", "http://example.com:80");
    check_host("[::ffff:1.2.3.4]:8", "http://[::ffff:1.2.3.4]:8");
    check_host("[fe80::1%eth0]", "http://[fe80::1%eth0]:11434");
    check_host("[1:2:3:4:5:6:7::]", "http://[1:2:3:4:5:6:7::]:11434");
    check_host("[v1.fe]", "http://v1.fe:11434");

    /* rejected like the Python client does (ValueError from urlsplit) */
    check_invalid_host("[::1");
    check_invalid_host("::1]");
    check_invalid_host("http://[nope]:80");
    check_invalid_host("[1.2.3.4]");
    check_invalid_host("[1::2::3]");
    check_invalid_host("[1:2:3:4:5:6:7:8:9]");
    check_invalid_host("[::1.2.3.04]");
    check_invalid_host("example.com:abc");
    check_invalid_host("example.com:65536");
    check_invalid_host("[::1]:99999");
    /* found by fuzz_host: the output of a host with unbalanced brackets changed when parsed again */
    check_invalid_host("[\xff[\xff]:\xff/]:\xff/");
    /* found by fuzz_host: valid brackets in the user info, broken ones in the host */
    check_invalid_host("https://user:pw@E[::1]:0@::][1E:p?q#f");
    check_host("[::1]@example.com", "http://example.com:11434");
}

/* ---- JSON ---- */

static void check_roundtrip(const char *input, const char *expected) {
    ollama_json_t *v = ollama_json_parse(input);
    CHECK(v != NULL);
    char *out = ollama_json_stringify(v);
    CHECK_STR(out, expected);
    ollama_free(out);
    ollama_json_free(v);
}

static void test_json(void) {
    check_roundtrip("null", "null");
    check_roundtrip(" true ", "true");
    check_roundtrip("[1, -2, 3.5, 1e3, 0.1, -0, 9007199254740993]", "[1,-2,3.5,1000,0.1,-0,9007199254740993]");
    /* found by fuzz_json: negative zero lost its sign after a round trip ("-0.0" -> "-0" -> "0") */
    check_roundtrip("[-0.0, -0, 0.0, -0e10]", "[-0,-0,0,-0]");
    check_roundtrip("-0", "-0");
    check_roundtrip("{\"a\": {\"b\": [\"x\", null]}, \"c\": false}", "{\"a\":{\"b\":[\"x\",null]},\"c\":false}");
    check_roundtrip("\"tab\\t quote\\\" slash\\/ nl\\n \\u0001\"", "\"tab\\t quote\\\" slash/ nl\\n \\u0001\"");
    check_roundtrip("\"\\u00e8 \\ud83d\\ude00\"", "\"\xc3\xa8 \xf0\x9f\x98\x80\"");
    check_roundtrip("\"caff\xc3\xa8\"", "\"caff\xc3\xa8\"");
    check_roundtrip("[]", "[]");
    check_roundtrip("{}", "{}");
    check_roundtrip("123456789012345678901234567890", "1.2345678901234568e+29");

    const char *invalid[] = {"", "nul", "[1,]", "{\"a\":1,}", "{a:1}", "01", "1.", "-", "\"abc", "[1 2]",
                             "\"\x01\"", "{\"a\" 1}", "[1]x", "\"\\x\"", "1e", NULL};
    for (int i = 0; invalid[i]; i++) {
        ollama_json_t *v = ollama_json_parse(invalid[i]);
        if (v) fprintf(stderr, "accepted invalid JSON: %s\n", invalid[i]);
        CHECK(v == NULL);
        ollama_json_free(v);
    }

    /* deep nesting is rejected instead of overflowing the stack */
    char *deep = malloc(100001);
    memset(deep, '[', 100000);
    deep[100000] = '\0';
    CHECK(ollama_json_parse(deep) == NULL);
    free(deep);

    ollama_json_t *v = ollama_json_parse("{\"i\": 42, \"d\": 2.5, \"s\": \"x\", \"b\": true, \"a\": [1,2], \"i\": 43}");
    CHECK(ollama_json_as_int(ollama_json_get(v, "i")) == 43);
    CHECK(ollama_json_as_number(ollama_json_get(v, "d")) == 2.5);
    CHECK_STR(ollama_json_as_string(ollama_json_get(v, "s")), "x");
    CHECK(ollama_json_as_bool(ollama_json_get(v, "b")));
    CHECK(ollama_json_size(ollama_json_get(v, "a")) == 2);
    CHECK(ollama_json_as_string(ollama_json_get(v, "i")) == NULL);
    CHECK(ollama_json_get(v, "missing") == NULL);
    CHECK(ollama_json_type(ollama_json_get(v, "a")) == OLLAMA_JSON_ARRAY);
    CHECK_STR(ollama_json_key_at(v, 1), "d");

    /* copies are deep */
    ollama_json_t *copy = ollama_json_copy(v);
    ollama_json_free(v);
    char *out = ollama_json_stringify(copy);
    CHECK_STR(out, "{\"i\":42,\"d\":2.5,\"s\":\"x\",\"b\":true,\"a\":[1,2],\"i\":43}");
    ollama_free(out);
    ollama_json_free(copy);

    /* building values */
    ollama_json_t *o = ollama_json_object();
    ollama_json_set_string(o, "name", "x");
    ollama_json_set_number(o, "temperature", 0.7);
    ollama_json_set_int(o, "num_ctx", 4096);
    ollama_json_set_bool(o, "flag", false);
    ollama_json_t *arr = ollama_json_set(o, "stop", ollama_json_array());
    ollama_json_append(arr, ollama_json_string("\n"));
    ollama_json_set_string(o, "name", "y"); /* replaces */
    ollama_json_set_number(o, "nan", strtod("nan", NULL)); /* NAN warns (C4756) with some MSVC headers */
    out = ollama_json_stringify(o);
    CHECK_STR(out, "{\"name\":\"y\",\"temperature\":0.7,\"num_ctx\":4096,\"flag\":false,\"stop\":[\"\\n\"],\"nan\":null}");
    ollama_free(out);
    CHECK(ollama_json_append(o, ollama_json_null()) == NULL); /* not an array */
    CHECK(ollama_json_set(arr, "k", ollama_json_null()) == NULL); /* not an object */
    ollama_json_free(o);

    /* numbers do not depend on the locale */
    if (setlocale(LC_NUMERIC, "it_IT.UTF-8") || setlocale(LC_NUMERIC, "de_DE.UTF-8") ||
        setlocale(LC_NUMERIC, "fr_FR.UTF-8") || setlocale(LC_NUMERIC, "it-IT") || setlocale(LC_NUMERIC, "de-DE")) {
        check_roundtrip("[0.5, 1.25e-3]", "[0.5,0.00125]");
        setlocale(LC_NUMERIC, "C");
    } else {
        printf("  (no comma-decimal locale installed, locale check skipped)\n");
    }
}

/* ---- base64 and SHA-256 ---- */

static void test_base64(void) {
    const char *plain[] = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
    const char *encoded[] = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
    for (int i = 0; i < 7; i++) {
        char *e = ollama_base64_encode(plain[i], strlen(plain[i]));
        CHECK_STR(e, encoded[i]);
        ollama_free(e);
        size_t n;
        unsigned char *d = ollama_base64_decode(encoded[i], &n);
        CHECK(d && n == strlen(plain[i]) && memcmp(d, plain[i], n) == 0);
        ollama_free(d);
    }
    CHECK(ollama_base64_decode("Zm9v!", NULL) == NULL);
}

static void check_sha256(const void *data, size_t len, const char *expected) {
    sha256_t s;
    unsigned char d[32];
    char hex[65];
    sha256_init(&s);
    sha256_update(&s, data, len);
    sha256_final(&s, d);
    for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", d[i]);
    CHECK_STR(hex, expected);
}

static void test_sha256(void) {
    check_sha256("", 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    check_sha256("abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const char *two_blocks = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    check_sha256(two_blocks, strlen(two_blocks), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    char *million = malloc(1000000);
    memset(million, 'a', 1000000);
    check_sha256(million, 1000000, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    free(million);
}

/* ---- request serialization ---- */

static char *serialize(ollama_status_t st, ollama_json_t **body) {
    CHECK(st == OLLAMA_OK);
    char *s = ollama_json_stringify(*body);
    ollama_json_free(*body);
    return s;
}

static void test_build_requests(void) {
    err_t e;
    err_init(&e);
    ollama_json_t *body;
    char *s;

    ollama_generate_request_t gen = {.model = "llama3.2", .prompt = "Why?"};
    s = serialize(build_generate(&gen, false, &e, &body), &body);
    CHECK_STR(s, "{\"model\":\"llama3.2\",\"prompt\":\"Why?\",\"stream\":false}");
    ollama_free(s);

    ollama_json_t *options = ollama_json_object();
    ollama_json_set_number(options, "temperature", 0);
    ollama_json_t *format = ollama_json_string("json");
    int context[] = {1, 2, 3};
    ollama_image_t images[] = {{.base64 = "aGk="}, {.data = "hi", .size = 2}};
    ollama_generate_request_t gen2 = {
        .model = "llava",
        .system = "Be brief.",
        .template_ = "{{ .Prompt }}",
        .context = context,
        .context_count = 3,
        .think = "high",
        .raw = true,
        .logprobs = true,
        .top_logprobs = 2,
        .format = format,
        .images = images,
        .images_count = 2,
        .options = options,
        .keep_alive = "-1",
    };
    s = serialize(build_generate(&gen2, true, &e, &body), &body);
    CHECK_STR(s, "{\"model\":\"llava\",\"system\":\"Be brief.\",\"template\":\"{{ .Prompt }}\",\"context\":[1,2,3],"
                 "\"stream\":true,\"raw\":true,\"think\":\"high\",\"logprobs\":true,\"top_logprobs\":2,"
                 "\"format\":\"json\",\"options\":{\"temperature\":0},\"keep_alive\":-1,"
                 "\"images\":[\"aGk=\",\"aGk=\"]}");
    ollama_free(s);

    ollama_json_t *args = ollama_json_parse("{\"city\":\"Rome\"}");
    ollama_tool_call_t calls[] = {{.function = {.name = "get_weather", .arguments = args}}};
    ollama_message_t messages[] = {
        {.role = "system", .content = "You are helpful."},
        {.role = "user", .content = "Weather?", .thinking = ""},
        {.role = "assistant", .content = "", .tool_calls = calls, .tool_calls_count = 1},
        {.role = "tool", .content = "Sunny", .tool_name = "get_weather"},
    };
    ollama_json_t *tool = ollama_tool_new("get_weather", "Get the weather");
    ollama_tool_add_parameter(tool, "city", "string", "The city", true);
    ollama_chat_request_t chat = {
        .model = "llama3.2",
        .messages = messages,
        .messages_count = 4,
        .tools = tool, /* a single tool object is wrapped in an array */
        .think = "true",
        .keep_alive = "5m",
    };
    s = serialize(build_chat(&chat, false, &e, &body), &body);
    CHECK_STR(s, "{\"model\":\"llama3.2\",\"messages\":[{\"role\":\"system\",\"content\":\"You are helpful.\"},"
                 "{\"role\":\"user\",\"content\":\"Weather?\"},{\"role\":\"assistant\",\"tool_calls\":[{\"function\":"
                 "{\"name\":\"get_weather\",\"arguments\":{\"city\":\"Rome\"}}}]},{\"role\":\"tool\",\"content\":"
                 "\"Sunny\",\"tool_name\":\"get_weather\"}],\"tools\":[{\"type\":\"function\",\"function\":{\"name\":"
                 "\"get_weather\",\"description\":\"Get the weather\",\"parameters\":{\"type\":\"object\","
                 "\"properties\":{\"city\":{\"type\":\"string\",\"description\":\"The city\"}},\"required\":"
                 "[\"city\"]}}}],\"stream\":false,\"think\":true,\"keep_alive\":\"5m\"}");
    ollama_free(s);

    const char *inputs[] = {"a", "b"};
    ollama_embed_request_t embed = {.model = "m", .inputs = inputs, .inputs_count = 2, .truncate = OLLAMA_FALSE,
                                    .dimensions = 64};
    s = serialize(build_embed(&embed, &e, &body), &body);
    CHECK_STR(s, "{\"model\":\"m\",\"input\":[\"a\",\"b\"],\"truncate\":false,\"dimensions\":64}");
    ollama_free(s);

    ollama_embed_request_t embed2 = {.model = "m"};
    s = serialize(build_embed(&embed2, &e, &body), &body);
    CHECK_STR(s, "{\"model\":\"m\",\"input\":\"\"}");
    ollama_free(s);

    s = serialize(build_pull_push("llama3.2", false, true, &e, &body), &body);
    CHECK_STR(s, "{\"model\":\"llama3.2\",\"insecure\":false,\"stream\":true}");
    ollama_free(s);

    ollama_json_t *files = ollama_json_object();
    ollama_json_set_string(files, "model.gguf", "sha256:abc");
    ollama_create_request_t create = {.model = "mario", .from_ = "llama3.2", .files = files, .system = "Mario"};
    s = serialize(build_create(&create, false, &e, &body), &body);
    CHECK_STR(s, "{\"model\":\"mario\",\"stream\":false,\"from\":\"llama3.2\",\"files\":{\"model.gguf\":"
                 "\"sha256:abc\"},\"system\":\"Mario\"}");
    ollama_free(s);

    /* errors */
    ollama_generate_request_t no_model = {.prompt = "x"};
    CHECK(build_generate(&no_model, false, &e, &body) == OLLAMA_ERROR_REQUEST && body == NULL);
    CHECK_STR(e.pub.message, "must provide a model");
    CHECK(build_chat(NULL, false, &e, &body) == OLLAMA_ERROR_REQUEST);
    ollama_message_t no_role[] = {{.content = "x"}};
    ollama_chat_request_t bad_chat = {.model = "m", .messages = no_role, .messages_count = 1};
    CHECK(build_chat(&bad_chat, false, &e, &body) == OLLAMA_ERROR_REQUEST);
    ollama_image_t missing_file[] = {{.path = "/nonexistent/cat.png"}};
    ollama_generate_request_t bad_image = {.model = "m", .images = missing_file, .images_count = 1};
    CHECK(build_generate(&bad_image, false, &e, &body) == OLLAMA_ERROR_IO);
    ollama_generate_request_t bad_count = {.model = "m", .images_count = 1};
    CHECK(build_generate(&bad_count, false, &e, &body) == OLLAMA_ERROR_REQUEST);

    err_free(&e);
    ollama_json_free(options);
    ollama_json_free(format);
    ollama_json_free(args);
    ollama_json_free(tool);
    ollama_json_free(files);
}

/* ---- response parsing ---- */

static void test_parse_responses(void) {
    ollama_chat_response_t *chat = parse_chat(ollama_json_parse(
        "{\"model\":\"llama3.2\",\"created_at\":\"2024-01-01T00:00:00Z\",\"message\":{\"role\":\"assistant\","
        "\"content\":\"\",\"thinking\":\"hmm\",\"tool_calls\":[{\"function\":{\"name\":\"f\",\"arguments\":"
        "{\"x\":1}}}]},\"done\":true,\"done_reason\":\"stop\",\"total_duration\":12345678901,\"eval_count\":7,"
        "\"logprobs\":[{\"token\":\"a\",\"logprob\":-0.5,\"top_logprobs\":[{\"token\":\"a\",\"logprob\":-0.5},"
        "{\"token\":\"b\",\"logprob\":-2}]}]}"));
    CHECK(chat != NULL);
    CHECK_STR(chat->model, "llama3.2");
    CHECK(chat->done);
    CHECK_STR(chat->done_reason, "stop");
    CHECK(chat->total_duration == 12345678901LL);
    CHECK(chat->eval_count == 7);
    CHECK_STR(chat->message.role, "assistant");
    CHECK_STR(chat->message.content, "");
    CHECK_STR(chat->message.thinking, "hmm");
    CHECK(chat->message.tool_calls_count == 1);
    CHECK_STR(chat->message.tool_calls[0].function.name, "f");
    CHECK(ollama_json_as_int(ollama_json_get(chat->message.tool_calls[0].function.arguments, "x")) == 1);
    CHECK(chat->logprobs_count == 1 && chat->logprobs[0].top_logprobs_count == 2);
    CHECK(chat->logprobs[0].top_logprobs[1].logprob == -2.0);
    CHECK(chat->json != NULL);

    /* deep copy into a history, then release the response */
    ollama_messages_t *history = ollama_messages_new();
    CHECK(ollama_messages_add(history, "user", "hi") == OLLAMA_OK);
    CHECK(ollama_messages_add_message(history, &chat->message) == OLLAMA_OK);
    ollama_response_free(chat);
    CHECK(history->count == 2);
    CHECK_STR(history->items[1].tool_calls[0].function.name, "f");
    CHECK(ollama_json_as_int(ollama_json_get(history->items[1].tool_calls[0].function.arguments, "x")) == 1);
    for (int i = 0; i < 20; i++) ollama_messages_add(history, "user", "more");
    CHECK(history->count == 22);
    ollama_messages_free(history);

    ollama_generate_response_t *gen = parse_generate(ollama_json_parse("{\"done\":false}"));
    CHECK(gen && gen->response && gen->response[0] == '\0' && gen->context == NULL && gen->context_count == 0);
    ollama_response_free(gen);

    ollama_list_response_t *list = parse_list(ollama_json_parse(
        "{\"models\":[{\"name\":\"a:latest\",\"size\":5,\"details\":{\"families\":[\"x\",1,\"y\"]}}]}"));
    CHECK(list && list->models_count == 1);
    CHECK_STR(list->models[0].model, "a:latest");
    CHECK(list->models[0].size == 5);
    CHECK(list->models[0].details.families_count == 2);
    CHECK_STR(list->models[0].details.families[1], "y");
    ollama_response_free(list);

    ollama_list_response_t *empty = parse_list(ollama_json_parse("[]"));
    CHECK(empty && empty->models_count == 0);
    ollama_response_free(empty);
    ollama_response_free(NULL);
}

/* Replays NDJSON lines through a chat stream as if they had been received. */
static void feed(ollama_stream_t *s, const char *lines) {
    buf_append(&s->data, lines, strlen(lines));
}

static void test_stream_message_after_failure(void) {
    ollama_stream_t *s = stream_new(STREAM_CHAT);
    s->finished = true;
    feed(s, "{\"message\":{\"role\":\"assistant\",\"content\":\"a\"}}\n");
    CHECK(ollama_stream_next_chat(s) != NULL);
    CHECK_STR(ollama_stream_message(s)->content, "a");

    /* the content grows (and moves), then appending the thinking runs out of memory */
    char line[600];
    char big[401];
    memset(big, 'b', 400);
    big[400] = '\0';
    snprintf(line, sizeof(line), "{\"message\":{\"content\":\"%s\",\"thinking\":\"t\"}}\n", big);
    feed(s, line);
    s->thinking.oom = true;
    CHECK(ollama_stream_next_chat(s) == NULL);
    CHECK(ollama_stream_error(s)->code == OLLAMA_ERROR_MEMORY);
    /* found by review: the message pointed to the freed content buffer */
    const ollama_message_t *m = ollama_stream_message(s);
    CHECK(m->content && strlen(m->content) == 401 && m->content[0] == 'a');
    CHECK(m->thinking == NULL);
    ollama_stream_free(s);
}

int main(void) {
    RUN(test_parse_host);
    RUN(test_json);
    RUN(test_base64);
    RUN(test_sha256);
    RUN(test_build_requests);
    RUN(test_parse_responses);
    RUN(test_stream_message_after_failure);
    return test_report();
}
