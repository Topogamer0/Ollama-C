/*
 * Response parsing, the C side of the Python *Response models.
 * Strings point into the response's JSON tree; arrays are allocated with
 * resp_alloc() so that ollama_response_free() releases everything at once.
 */
#include "internal.h"

static const char EMPTY[] = "";

static const char *jstr(const ollama_json_t *o, const char *key) { return ollama_json_as_string(ollama_json_get(o, key)); }

static int64_t jint(const ollama_json_t *o, const char *key) { return ollama_json_as_int(ollama_json_get(o, key)); }

static double jnum(const ollama_json_t *o, const char *key) { return ollama_json_as_number(ollama_json_get(o, key)); }

static bool jbool(const ollama_json_t *o, const char *key) { return ollama_json_as_bool(ollama_json_get(o, key)); }

static size_t array_size(const ollama_json_t *v) {
    return ollama_json_type(v) == OLLAMA_JSON_ARRAY ? ollama_json_size(v) : 0;
}

static void *finish(void *resp) {
    if (resp && resp_failed(resp)) {
        ollama_response_free(resp);
        return NULL;
    }
    return resp;
}

static const char *const *str_array(void *resp, const ollama_json_t *arr, size_t *count) {
    size_t n = array_size(arr), k = 0;
    const char **items = resp_alloc(resp, n, sizeof(*items));
    for (size_t i = 0; items && i < n; i++) {
        const char *s = ollama_json_as_string(ollama_json_at(arr, i));
        if (s) items[k++] = s;
    }
    *count = k;
    return items;
}

static void parse_details(void *resp, const ollama_json_t *j, ollama_model_details_t *d) {
    d->parent_model = jstr(j, "parent_model");
    d->format = jstr(j, "format");
    d->family = jstr(j, "family");
    d->families = str_array(resp, ollama_json_get(j, "families"), &d->families_count);
    d->parameter_size = jstr(j, "parameter_size");
    d->quantization_level = jstr(j, "quantization_level");
}

static const ollama_logprob_t *parse_logprobs(void *resp, const ollama_json_t *arr, size_t *count) {
    size_t n = array_size(arr);
    ollama_logprob_t *items = resp_alloc(resp, n, sizeof(*items));
    *count = items ? n : 0;
    for (size_t i = 0; items && i < n; i++) {
        const ollama_json_t *item = ollama_json_at(arr, i);
        items[i].token = jstr(item, "token");
        items[i].logprob = jnum(item, "logprob");
        const ollama_json_t *top = ollama_json_get(item, "top_logprobs");
        size_t m = array_size(top);
        ollama_token_logprob_t *alts = resp_alloc(resp, m, sizeof(*alts));
        for (size_t k = 0; alts && k < m; k++) {
            alts[k].token = jstr(ollama_json_at(top, k), "token");
            alts[k].logprob = jnum(ollama_json_at(top, k), "logprob");
        }
        items[i].top_logprobs = alts;
        items[i].top_logprobs_count = alts ? m : 0;
    }
    return items;
}

static void parse_message(void *resp, const ollama_json_t *j, ollama_message_t *m) {
    m->role = jstr(j, "role");
    m->content = jstr(j, "content");
    if (!m->content) m->content = EMPTY;
    m->thinking = jstr(j, "thinking");
    m->tool_name = jstr(j, "tool_name");
    const ollama_json_t *calls = ollama_json_get(j, "tool_calls");
    size_t n = array_size(calls);
    ollama_tool_call_t *items = resp_alloc(resp, n, sizeof(*items));
    for (size_t i = 0; items && i < n; i++) {
        const ollama_json_t *fn = ollama_json_get(ollama_json_at(calls, i), "function");
        items[i].function.name = jstr(fn, "name");
        items[i].function.arguments = ollama_json_get(fn, "arguments");
    }
    m->tool_calls = items;
    m->tool_calls_count = items ? n : 0;
}

/* Fields shared by generate, chat and embed responses (BaseGenerateResponse). */
#define PARSE_BASE(r, j)                                               \
    do {                                                               \
        (r)->model = jstr(j, "model");                                 \
        (r)->created_at = jstr(j, "created_at");                       \
        (r)->done = jbool(j, "done");                                  \
        (r)->done_reason = jstr(j, "done_reason");                     \
        (r)->total_duration = jint(j, "total_duration");               \
        (r)->load_duration = jint(j, "load_duration");                 \
        (r)->prompt_eval_count = jint(j, "prompt_eval_count");         \
        (r)->prompt_eval_duration = jint(j, "prompt_eval_duration");   \
        (r)->eval_count = jint(j, "eval_count");                       \
        (r)->eval_duration = jint(j, "eval_duration");                 \
        (r)->json = (j);                                               \
    } while (0)

void *parse_generate(ollama_json_t *json) {
    ollama_generate_response_t *r = resp_new(sizeof(*r), json);
    if (!r) return NULL;
    PARSE_BASE(r, json);
    r->response = jstr(json, "response");
    if (!r->response) r->response = EMPTY;
    r->thinking = jstr(json, "thinking");
    const ollama_json_t *context = ollama_json_get(json, "context");
    size_t n = array_size(context);
    int *tokens = resp_alloc(r, n, sizeof(*tokens));
    for (size_t i = 0; tokens && i < n; i++) tokens[i] = (int)ollama_json_as_int(ollama_json_at(context, i));
    r->context = tokens;
    r->context_count = tokens ? n : 0;
    r->logprobs = parse_logprobs(r, ollama_json_get(json, "logprobs"), &r->logprobs_count);
    r->image = jstr(json, "image");
    r->completed = jint(json, "completed");
    r->total = jint(json, "total");
    return finish(r);
}

void *parse_chat(ollama_json_t *json) {
    ollama_chat_response_t *r = resp_new(sizeof(*r), json);
    if (!r) return NULL;
    PARSE_BASE(r, json);
    parse_message(r, ollama_json_get(json, "message"), &r->message);
    r->logprobs = parse_logprobs(r, ollama_json_get(json, "logprobs"), &r->logprobs_count);
    return finish(r);
}

static const double *number_array(void *resp, const ollama_json_t *arr, size_t *count) {
    size_t n = array_size(arr);
    double *values = resp_alloc(resp, n, sizeof(*values));
    for (size_t i = 0; values && i < n; i++) values[i] = ollama_json_as_number(ollama_json_at(arr, i));
    *count = values ? n : 0;
    return values;
}

void *parse_embed(ollama_json_t *json) {
    ollama_embed_response_t *r = resp_new(sizeof(*r), json);
    if (!r) return NULL;
    PARSE_BASE(r, json);
    const ollama_json_t *arr = ollama_json_get(json, "embeddings");
    size_t n = array_size(arr);
    ollama_embedding_t *items = resp_alloc(r, n, sizeof(*items));
    for (size_t i = 0; items && i < n; i++) items[i].values = number_array(r, ollama_json_at(arr, i), &items[i].size);
    r->embeddings = items;
    r->embeddings_count = items ? n : 0;
    return finish(r);
}

void *parse_embeddings(ollama_json_t *json) {
    ollama_embeddings_response_t *r = resp_new(sizeof(*r), json);
    if (!r) return NULL;
    r->embedding = number_array(r, ollama_json_get(json, "embedding"), &r->embedding_size);
    r->json = json;
    return finish(r);
}

void *parse_progress(ollama_json_t *json) {
    ollama_progress_response_t *r = resp_new(sizeof(*r), json);
    if (!r) return NULL;
    r->status = jstr(json, "status");
    r->completed = jint(json, "completed");
    r->total = jint(json, "total");
    r->digest = jstr(json, "digest");
    r->json = json;
    return finish(r);
}

void *parse_list(ollama_json_t *json) {
    ollama_list_response_t *r = resp_new(sizeof(*r), json);
    if (!r) return NULL;
    const ollama_json_t *arr = ollama_json_get(json, "models");
    size_t n = array_size(arr);
    ollama_list_model_t *models = resp_alloc(r, n, sizeof(*models));
    for (size_t i = 0; models && i < n; i++) {
        const ollama_json_t *m = ollama_json_at(arr, i);
        models[i].model = jstr(m, "model");
        if (!models[i].model) models[i].model = jstr(m, "name");
        models[i].modified_at = jstr(m, "modified_at");
        models[i].digest = jstr(m, "digest");
        models[i].size = jint(m, "size");
        parse_details(r, ollama_json_get(m, "details"), &models[i].details);
    }
    r->models = models;
    r->models_count = models ? n : 0;
    r->json = json;
    return finish(r);
}

void *parse_show(ollama_json_t *json) {
    ollama_show_response_t *r = resp_new(sizeof(*r), json);
    if (!r) return NULL;
    r->modified_at = jstr(json, "modified_at");
    r->template_ = jstr(json, "template");
    r->modelfile = jstr(json, "modelfile");
    r->license = jstr(json, "license");
    parse_details(r, ollama_json_get(json, "details"), &r->details);
    r->modelinfo = ollama_json_get(json, "model_info");
    r->parameters = jstr(json, "parameters");
    r->capabilities = str_array(r, ollama_json_get(json, "capabilities"), &r->capabilities_count);
    r->json = json;
    return finish(r);
}

void *parse_ps(ollama_json_t *json) {
    ollama_process_response_t *r = resp_new(sizeof(*r), json);
    if (!r) return NULL;
    const ollama_json_t *arr = ollama_json_get(json, "models");
    size_t n = array_size(arr);
    ollama_process_model_t *models = resp_alloc(r, n, sizeof(*models));
    for (size_t i = 0; models && i < n; i++) {
        const ollama_json_t *m = ollama_json_at(arr, i);
        models[i].model = jstr(m, "model");
        models[i].name = jstr(m, "name");
        models[i].digest = jstr(m, "digest");
        models[i].expires_at = jstr(m, "expires_at");
        models[i].size = jint(m, "size");
        models[i].size_vram = jint(m, "size_vram");
        parse_details(r, ollama_json_get(m, "details"), &models[i].details);
        models[i].context_length = jint(m, "context_length");
    }
    r->models = models;
    r->models_count = models ? n : 0;
    r->json = json;
    return finish(r);
}

void *parse_web_search(ollama_json_t *json) {
    ollama_web_search_response_t *r = resp_new(sizeof(*r), json);
    if (!r) return NULL;
    const ollama_json_t *arr = ollama_json_get(json, "results");
    size_t n = array_size(arr);
    ollama_web_search_result_t *results = resp_alloc(r, n, sizeof(*results));
    for (size_t i = 0; results && i < n; i++) {
        const ollama_json_t *item = ollama_json_at(arr, i);
        results[i].content = jstr(item, "content");
        results[i].title = jstr(item, "title");
        results[i].url = jstr(item, "url");
    }
    r->results = results;
    r->results_count = results ? n : 0;
    r->json = json;
    return finish(r);
}

void *parse_web_fetch(ollama_json_t *json) {
    ollama_web_fetch_response_t *r = resp_new(sizeof(*r), json);
    if (!r) return NULL;
    r->title = jstr(json, "title");
    r->content = jstr(json, "content");
    r->links = str_array(r, ollama_json_get(json, "links"), &r->links_count);
    r->json = json;
    return finish(r);
}

void *parse_systemone(ollama_json_t *json) {
    ollama_systemone_response_t *r = resp_new(sizeof(*r), json);
    if (!r) return NULL;
    r->model = jstr(json, "model");
    const ollama_json_t *obj = ollama_json_get(json, "answers");
    size_t n = ollama_json_type(obj) == OLLAMA_JSON_OBJECT ? ollama_json_size(obj) : 0;
    ollama_systemone_answer_t *answers = resp_alloc(r, n, sizeof(*answers));
    for (size_t i = 0; answers && i < n; i++) {
        const ollama_json_t *a = ollama_json_at(obj, i);
        answers[i].name = ollama_json_key_at(obj, i);
        answers[i].type = jstr(a, "type");
        answers[i].noul = jnum(a, "noul");
        answers[i].choice = jstr(a, "choice");
        answers[i].score = jnum(a, "score");
        answers[i].confidence = jnum(a, "confidence");
        answers[i].probabilities = ollama_json_get(a, "probabilities");
        answers[i].legend = ollama_json_get(a, "legend");
    }
    r->answers = answers;
    r->answers_count = answers ? n : 0;
    const ollama_json_t *usage = ollama_json_get(json, "usage");
    r->usage.input_tokens = jint(usage, "input_tokens");
    r->usage.output_tokens = jint(usage, "output_tokens");
    r->json = json;
    return finish(r);
}
