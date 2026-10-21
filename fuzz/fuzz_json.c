/*
 * Fuzz target for the JSON parser and the response parsers.
 *
 * Every input is used three ways:
 *   1. as a JSON document, which is parsed, serialized, parsed again and
 *      serialized again: the two serializations must be identical;
 *   2. as the body of a regular response, handed to every response parser;
 *   3. as the body of a streaming response (NDJSON), replayed through the
 *      iterators of generate, chat and progress streams.
 * Every field of the results is read, so that the sanitizers catch dangling
 * or out of bounds pointers.
 */
#include "internal.h"

#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static volatile size_t sink;

static void touch_str(const char *s) {
    if (s) sink += strlen(s);
}

static void touch_json(const ollama_json_t *v) {
    char *s = ollama_json_stringify(v);
    touch_str(s);
    ollama_free(s);
}

static void touch_strings(const char *const *items, size_t count) {
    for (size_t i = 0; i < count; i++) touch_str(items[i]);
}

static void touch_details(const ollama_model_details_t *d) {
    touch_str(d->parent_model);
    touch_str(d->format);
    touch_str(d->family);
    touch_strings(d->families, d->families_count);
    touch_str(d->parameter_size);
    touch_str(d->quantization_level);
}

static void touch_logprobs(const ollama_logprob_t *items, size_t count) {
    for (size_t i = 0; i < count; i++) {
        touch_str(items[i].token);
        sink += (size_t)(items[i].logprob != 0);
        for (size_t k = 0; k < items[i].top_logprobs_count; k++) touch_str(items[i].top_logprobs[k].token);
    }
}

static void touch_message(const ollama_message_t *m) {
    touch_str(m->role);
    touch_str(m->content);
    touch_str(m->thinking);
    touch_str(m->tool_name);
    for (size_t i = 0; i < m->tool_calls_count; i++) {
        touch_str(m->tool_calls[i].function.name);
        touch_json(m->tool_calls[i].function.arguments);
    }
}

static void touch_generate(const ollama_generate_response_t *r) {
    if (!r) return;
    touch_str(r->model);
    touch_str(r->created_at);
    touch_str(r->done_reason);
    touch_str(r->response);
    touch_str(r->thinking);
    touch_str(r->image);
    for (size_t i = 0; i < r->context_count; i++) sink += (size_t)r->context[i];
    touch_logprobs(r->logprobs, r->logprobs_count);
}

static void touch_chat(const ollama_chat_response_t *r) {
    if (!r) return;
    touch_str(r->model);
    touch_str(r->created_at);
    touch_str(r->done_reason);
    touch_message(&r->message);
    touch_logprobs(r->logprobs, r->logprobs_count);
}

static void touch_progress(const ollama_progress_response_t *r) {
    if (!r) return;
    touch_str(r->status);
    touch_str(r->digest);
}

/* 1. serialization round trip */
static void check_roundtrip(const ollama_json_t *doc) {
    char *first = ollama_json_stringify(doc);
    if (!first) return;
    ollama_json_t *again = ollama_json_parse(first);
    if (!again) abort(); /* the serializer produced invalid JSON */
    char *second = ollama_json_stringify(again);
    if (!second || strcmp(first, second) != 0) abort(); /* serialization is not stable */
    ollama_json_t *copy = ollama_json_copy(doc);
    char *third = ollama_json_stringify(copy);
    if (!third || strcmp(first, third) != 0) abort(); /* the copy differs from the original */
    ollama_free(first);
    ollama_free(second);
    ollama_free(third);
    ollama_json_free(again);
    ollama_json_free(copy);
}

/* 2. regular responses */
static void parse_responses(const ollama_json_t *doc) {
    ollama_generate_response_t *gen = parse_generate(ollama_json_copy(doc));
    touch_generate(gen);
    ollama_response_free(gen);

    ollama_chat_response_t *chat = parse_chat(ollama_json_copy(doc));
    touch_chat(chat);
    if (chat) {
        /* the history keeps deep copies of response messages */
        ollama_messages_t *history = ollama_messages_new();
        ollama_messages_add_message(history, &chat->message);
        ollama_response_free(chat);
        if (history && history->count) touch_message(&history->items[0]);
        ollama_messages_free(history);
    }

    ollama_embed_response_t *embed = parse_embed(ollama_json_copy(doc));
    if (embed) {
        touch_str(embed->model);
        for (size_t i = 0; i < embed->embeddings_count; i++)
            for (size_t k = 0; k < embed->embeddings[i].size; k++)
                sink += (size_t)(embed->embeddings[i].values[k] != 0);
    }
    ollama_response_free(embed);

    ollama_embeddings_response_t *embeddings = parse_embeddings(ollama_json_copy(doc));
    if (embeddings)
        for (size_t i = 0; i < embeddings->embedding_size; i++) sink += (size_t)(embeddings->embedding[i] != 0);
    ollama_response_free(embeddings);

    ollama_progress_response_t *progress = parse_progress(ollama_json_copy(doc));
    touch_progress(progress);
    ollama_response_free(progress);

    ollama_list_response_t *list = parse_list(ollama_json_copy(doc));
    for (size_t i = 0; list && i < list->models_count; i++) {
        touch_str(list->models[i].model);
        touch_str(list->models[i].modified_at);
        touch_str(list->models[i].digest);
        touch_details(&list->models[i].details);
    }
    ollama_response_free(list);

    ollama_show_response_t *show = parse_show(ollama_json_copy(doc));
    if (show) {
        touch_str(show->modified_at);
        touch_str(show->template_);
        touch_str(show->modelfile);
        touch_str(show->license);
        touch_str(show->parameters);
        touch_details(&show->details);
        touch_json(show->modelinfo);
        touch_strings(show->capabilities, show->capabilities_count);
    }
    ollama_response_free(show);

    ollama_process_response_t *ps = parse_ps(ollama_json_copy(doc));
    for (size_t i = 0; ps && i < ps->models_count; i++) {
        touch_str(ps->models[i].model);
        touch_str(ps->models[i].name);
        touch_str(ps->models[i].digest);
        touch_str(ps->models[i].expires_at);
        touch_details(&ps->models[i].details);
    }
    ollama_response_free(ps);

    ollama_web_search_response_t *search = parse_web_search(ollama_json_copy(doc));
    for (size_t i = 0; search && i < search->results_count; i++) {
        touch_str(search->results[i].content);
        touch_str(search->results[i].title);
        touch_str(search->results[i].url);
    }
    ollama_response_free(search);

    ollama_web_fetch_response_t *fetch = parse_web_fetch(ollama_json_copy(doc));
    if (fetch) {
        touch_str(fetch->title);
        touch_str(fetch->content);
        touch_strings(fetch->links, fetch->links_count);
    }
    ollama_response_free(fetch);

    ollama_systemone_response_t *one = parse_systemone(ollama_json_copy(doc));
    for (size_t i = 0; one && i < one->answers_count; i++) {
        touch_str(one->answers[i].name);
        touch_str(one->answers[i].type);
        touch_str(one->answers[i].choice);
        touch_json(one->answers[i].probabilities);
        touch_json(one->answers[i].legend);
    }
    ollama_response_free(one);
}

/* 3. streaming responses, as received after the stream has been opened */
static void replay_stream(const uint8_t *data, size_t size, stream_kind_t kind) {
    ollama_stream_t *s = stream_new(kind);
    if (!s) return;
    s->finished = true;
    s->result = CURLE_OK;
    /* like the curl write callback, which never runs for an empty body */
    if (size == 0 || buf_append(&s->data, data, size)) {
        switch (kind) {
        case STREAM_GENERATE: {
            const ollama_generate_response_t *part;
            while ((part = ollama_stream_next_generate(s)) != NULL) touch_generate(part);
            break;
        }
        case STREAM_CHAT: {
            const ollama_chat_response_t *part;
            while ((part = ollama_stream_next_chat(s)) != NULL) touch_chat(part);
            const ollama_message_t *full = ollama_stream_message(s);
            touch_message(full);
            ollama_messages_t *history = ollama_messages_new();
            ollama_messages_add_message(history, full);
            ollama_messages_free(history);
            break;
        }
        case STREAM_PROGRESS: {
            const ollama_progress_response_t *part;
            while ((part = ollama_stream_next_progress(s)) != NULL) touch_progress(part);
            break;
        }
        }
    }
    touch_str(ollama_stream_error(s)->message);
    ollama_stream_free(s);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    ollama_json_t *doc = json_parse_n((const char *)data, size);
    if (doc) {
        check_roundtrip(doc);
        parse_responses(doc);
        ollama_json_free(doc);
    }
    replay_stream(data, size, STREAM_GENERATE);
    replay_stream(data, size, STREAM_CHAT);
    replay_stream(data, size, STREAM_PROGRESS);
    return 0;
}
