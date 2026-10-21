/* Request serialization, the C side of the Python *Request models. */
#include "internal.h"

#include <stdlib.h>
#include <string.h>

static ollama_status_t oom(err_t *e) { return err_set(e, OLLAMA_ERROR_MEMORY, -1, "out of memory"); }

static ollama_status_t check_model(const char *model, err_t *e) {
    if (!model || !*model) return err_set(e, OLLAMA_ERROR_REQUEST, -1, "must provide a model");
    return OLLAMA_OK;
}

static ollama_status_t check_request(const void *request, const char *model, err_t *e) {
    if (!request) return err_set(e, OLLAMA_ERROR_REQUEST, -1, "request is NULL");
    return check_model(model, e);
}

static ollama_status_t check_array(const void *items, size_t count, const char *name, err_t *e) {
    if (count > 0 && !items) return err_set(e, OLLAMA_ERROR_REQUEST, -1, "%s_count is set but %s is NULL", name, name);
    return OLLAMA_OK;
}

/* ---- fields; each returns false when out of memory ---- */

static bool put_str(ollama_json_t *o, const char *key, const char *value) {
    return !value || ollama_json_set_string(o, key, value) != NULL;
}

/* Messages skip empty fields, like _copy_messages() in the Python client. */
static bool put_nonempty(ollama_json_t *o, const char *key, const char *value) {
    return !value || !*value || ollama_json_set_string(o, key, value) != NULL;
}

static bool put_bool(ollama_json_t *o, const char *key, bool value) {
    return ollama_json_set_bool(o, key, value) != NULL;
}

static bool put_positive(ollama_json_t *o, const char *key, int value) {
    return value <= 0 || ollama_json_set_int(o, key, value) != NULL;
}

static bool put_copy(ollama_json_t *o, const char *key, const ollama_json_t *value) {
    return !value || ollama_json_set(o, key, ollama_json_copy(value)) != NULL;
}

/* "5m" is sent as a string, "300" or "-1" as a number. */
static bool put_keep_alive(ollama_json_t *o, const char *keep_alive) {
    if (!keep_alive) return true;
    ollama_json_t *v = ollama_json_parse(keep_alive);
    if (ollama_json_type(v) != OLLAMA_JSON_NUMBER) {
        ollama_json_free(v);
        v = ollama_json_string(keep_alive);
    }
    return ollama_json_set(o, "keep_alive", v) != NULL;
}

/* "true" / "false" are sent as booleans, levels such as "high" as strings. */
static bool put_think(ollama_json_t *o, const char *think) {
    if (!think) return true;
    if (ascii_ieq(think, "true")) return put_bool(o, "think", true);
    if (ascii_ieq(think, "false")) return put_bool(o, "think", false);
    return put_str(o, "think", think);
}

static bool put_context(ollama_json_t *o, const int *context, size_t count) {
    if (count == 0) return true;
    ollama_json_t *arr = ollama_json_set(o, "context", ollama_json_array());
    for (size_t i = 0; arr && i < count; i++)
        if (!ollama_json_append(arr, ollama_json_int(context[i]))) return false;
    return arr != NULL;
}

static bool put_tools(ollama_json_t *o, const ollama_json_t *tools) {
    if (!tools) return true;
    if (ollama_json_type(tools) == OLLAMA_JSON_ARRAY) return put_copy(o, "tools", tools);
    /* a single tool object */
    ollama_json_t *arr = ollama_json_set(o, "tools", ollama_json_array());
    return arr && ollama_json_append(arr, ollama_json_copy(tools)) != NULL;
}

/* ---- images and messages ---- */

static ollama_status_t image_json(const ollama_image_t *img, err_t *e, ollama_json_t **out) {
    char *encoded = NULL;
    *out = NULL;
    if (img->base64) {
        *out = ollama_json_string(img->base64);
        return *out ? OLLAMA_OK : oom(e);
    }
    if (img->path) {
        buf_t b = {0};
        if (!read_file(img->path, &b)) {
            bool no_memory = b.oom;
            buf_free(&b);
            if (no_memory) return oom(e);
            return err_set(e, OLLAMA_ERROR_IO, -1, "could not read image file '%s'", img->path);
        }
        encoded = ollama_base64_encode(b.data, b.len);
        buf_free(&b);
    } else if (img->data) {
        encoded = ollama_base64_encode(img->data, img->size);
    } else {
        return err_set(e, OLLAMA_ERROR_REQUEST, -1, "invalid image: set path, data or base64");
    }
    if (!encoded) return oom(e);
    *out = ollama_json_string(encoded);
    free(encoded);
    return *out ? OLLAMA_OK : oom(e);
}

static ollama_status_t put_images(ollama_json_t *o, const ollama_image_t *images, size_t count, err_t *e) {
    if (count == 0) return OLLAMA_OK;
    ollama_status_t st = check_array(images, count, "images", e);
    if (st != OLLAMA_OK) return st;
    ollama_json_t *arr = ollama_json_set(o, "images", ollama_json_array());
    if (!arr) return oom(e);
    for (size_t i = 0; i < count; i++) {
        ollama_json_t *value;
        st = image_json(&images[i], e, &value);
        if (st != OLLAMA_OK) return st;
        if (!ollama_json_append(arr, value)) return oom(e);
    }
    return OLLAMA_OK;
}

static ollama_status_t put_tool_calls(ollama_json_t *o, const ollama_tool_call_t *calls, size_t count, err_t *e) {
    if (count == 0) return OLLAMA_OK;
    ollama_status_t st = check_array(calls, count, "tool_calls", e);
    if (st != OLLAMA_OK) return st;
    ollama_json_t *arr = ollama_json_set(o, "tool_calls", ollama_json_array());
    if (!arr) return oom(e);
    for (size_t i = 0; i < count; i++) {
        ollama_json_t *call = ollama_json_append(arr, ollama_json_object());
        ollama_json_t *fn = ollama_json_set(call, "function", ollama_json_object());
        const ollama_json_t *args = calls[i].function.arguments;
        bool ok = fn && put_str(fn, "name", calls[i].function.name) &&
                  ollama_json_set(fn, "arguments", args ? ollama_json_copy(args) : ollama_json_object()) != NULL;
        if (!ok) return oom(e);
    }
    return OLLAMA_OK;
}

static ollama_status_t message_json(const ollama_message_t *m, err_t *e, ollama_json_t **out) {
    *out = NULL;
    if (!m->role || !*m->role) return err_set(e, OLLAMA_ERROR_REQUEST, -1, "message role is required");
    ollama_json_t *o = ollama_json_object();
    if (!o) return oom(e);
    ollama_status_t st = OLLAMA_OK;
    if (!put_str(o, "role", m->role) || !put_nonempty(o, "content", m->content) ||
        !put_nonempty(o, "thinking", m->thinking))
        st = oom(e);
    if (st == OLLAMA_OK) st = put_images(o, m->images, m->images_count, e);
    if (st == OLLAMA_OK && !put_nonempty(o, "tool_name", m->tool_name)) st = oom(e);
    if (st == OLLAMA_OK) st = put_tool_calls(o, m->tool_calls, m->tool_calls_count, e);
    if (st != OLLAMA_OK) {
        ollama_json_free(o);
        return st;
    }
    *out = o;
    return OLLAMA_OK;
}

static ollama_status_t put_messages(ollama_json_t *o, const ollama_message_t *messages, size_t count, err_t *e) {
    ollama_status_t st = check_array(messages, count, "messages", e);
    if (st != OLLAMA_OK) return st;
    ollama_json_t *arr = ollama_json_set(o, "messages", ollama_json_array());
    if (!arr) return oom(e);
    for (size_t i = 0; i < count; i++) {
        ollama_json_t *value;
        st = message_json(&messages[i], e, &value);
        if (st != OLLAMA_OK) return st;
        if (!ollama_json_append(arr, value)) return oom(e);
    }
    return OLLAMA_OK;
}

/* Hands over the request object, or frees it after a failure. */
static ollama_status_t done(ollama_json_t *o, ollama_status_t st, ollama_json_t **out) {
    if (st != OLLAMA_OK) {
        ollama_json_free(o);
        return st;
    }
    *out = o;
    return OLLAMA_OK;
}

/* ---- requests ---- */

ollama_status_t build_generate(const ollama_generate_request_t *r, bool stream, err_t *e, ollama_json_t **out) {
    *out = NULL;
    ollama_status_t st = check_request(r, r ? r->model : NULL, e);
    if (st == OLLAMA_OK) st = check_array(r->context, r->context_count, "context", e);
    if (st != OLLAMA_OK) return st;

    ollama_json_t *o = ollama_json_object();
    if (!o) return oom(e);
    bool ok = put_str(o, "model", r->model) && put_str(o, "prompt", r->prompt) && put_str(o, "suffix", r->suffix) &&
              put_str(o, "system", r->system) && put_str(o, "template", r->template_) &&
              put_context(o, r->context, r->context_count) && put_bool(o, "stream", stream) &&
              (!r->raw || put_bool(o, "raw", true)) && put_think(o, r->think) &&
              (!r->logprobs || put_bool(o, "logprobs", true)) && put_positive(o, "top_logprobs", r->top_logprobs) &&
              put_copy(o, "format", r->format) && put_copy(o, "options", r->options) &&
              put_keep_alive(o, r->keep_alive) && put_positive(o, "width", r->width) &&
              put_positive(o, "height", r->height) && put_positive(o, "steps", r->steps);
    st = ok ? put_images(o, r->images, r->images_count, e) : oom(e);
    return done(o, st, out);
}

ollama_status_t build_chat(const ollama_chat_request_t *r, bool stream, err_t *e, ollama_json_t **out) {
    *out = NULL;
    ollama_status_t st = check_request(r, r ? r->model : NULL, e);
    if (st != OLLAMA_OK) return st;

    ollama_json_t *o = ollama_json_object();
    if (!o) return oom(e);
    st = put_str(o, "model", r->model) ? put_messages(o, r->messages, r->messages_count, e) : oom(e);
    if (st == OLLAMA_OK) {
        bool ok = put_tools(o, r->tools) && put_bool(o, "stream", stream) && put_think(o, r->think) &&
                  (!r->logprobs || put_bool(o, "logprobs", true)) &&
                  put_positive(o, "top_logprobs", r->top_logprobs) && put_copy(o, "format", r->format) &&
                  put_copy(o, "options", r->options) && put_keep_alive(o, r->keep_alive);
        if (!ok) st = oom(e);
    }
    return done(o, st, out);
}

ollama_status_t build_embed(const ollama_embed_request_t *r, err_t *e, ollama_json_t **out) {
    *out = NULL;
    ollama_status_t st = check_request(r, r ? r->model : NULL, e);
    if (st == OLLAMA_OK) st = check_array(r->inputs, r->inputs_count, "inputs", e);
    if (st != OLLAMA_OK) return st;

    ollama_json_t *o = ollama_json_object();
    if (!o) return oom(e);
    bool ok = put_str(o, "model", r->model);
    if (ok && r->inputs_count > 0) {
        ollama_json_t *arr = ollama_json_set(o, "input", ollama_json_array());
        ok = arr != NULL;
        for (size_t i = 0; ok && i < r->inputs_count; i++)
            ok = ollama_json_append(arr, ollama_json_string(r->inputs[i] ? r->inputs[i] : "")) != NULL;
    } else if (ok) {
        ok = put_str(o, "input", r->input ? r->input : "");
    }
    ok = ok && (r->truncate == OLLAMA_DEFAULT || put_bool(o, "truncate", r->truncate == OLLAMA_TRUE)) &&
         put_copy(o, "options", r->options) && put_keep_alive(o, r->keep_alive) &&
         put_positive(o, "dimensions", r->dimensions);
    return done(o, ok ? OLLAMA_OK : oom(e), out);
}

ollama_status_t build_embeddings(const ollama_embeddings_request_t *r, err_t *e, ollama_json_t **out) {
    *out = NULL;
    ollama_status_t st = check_request(r, r ? r->model : NULL, e);
    if (st != OLLAMA_OK) return st;

    ollama_json_t *o = ollama_json_object();
    if (!o) return oom(e);
    bool ok = put_str(o, "model", r->model) && put_str(o, "prompt", r->prompt) &&
              put_copy(o, "options", r->options) && put_keep_alive(o, r->keep_alive);
    return done(o, ok ? OLLAMA_OK : oom(e), out);
}

ollama_status_t build_pull_push(const char *model, bool insecure, bool stream, err_t *e, ollama_json_t **out) {
    *out = NULL;
    ollama_status_t st = check_model(model, e);
    if (st != OLLAMA_OK) return st;

    ollama_json_t *o = ollama_json_object();
    if (!o) return oom(e);
    bool ok = put_str(o, "model", model) && put_bool(o, "insecure", insecure) && put_bool(o, "stream", stream);
    return done(o, ok ? OLLAMA_OK : oom(e), out);
}

ollama_status_t build_create(const ollama_create_request_t *r, bool stream, err_t *e, ollama_json_t **out) {
    *out = NULL;
    ollama_status_t st = check_request(r, r ? r->model : NULL, e);
    if (st != OLLAMA_OK) return st;

    ollama_json_t *o = ollama_json_object();
    if (!o) return oom(e);
    bool ok = put_str(o, "model", r->model) && put_bool(o, "stream", stream) &&
              put_str(o, "quantize", r->quantize) && put_str(o, "from", r->from_) &&
              put_copy(o, "files", r->files) && put_copy(o, "adapters", r->adapters) &&
              put_str(o, "template", r->template_) && put_str(o, "license", r->license) &&
              put_str(o, "system", r->system) && put_copy(o, "parameters", r->parameters);
    st = ok ? OLLAMA_OK : oom(e);
    if (st == OLLAMA_OK && r->messages_count > 0) st = put_messages(o, r->messages, r->messages_count, e);
    return done(o, st, out);
}

ollama_status_t build_systemone(const ollama_systemone_request_t *r, err_t *e, ollama_json_t **out) {
    *out = NULL;
    ollama_status_t st = check_request(r, r ? r->model : NULL, e);
    if (st != OLLAMA_OK) return st;
    if (!r->state) return err_set(e, OLLAMA_ERROR_REQUEST, -1, "must provide a state");
    if (ollama_json_type(r->questions) != OLLAMA_JSON_OBJECT)
        return err_set(e, OLLAMA_ERROR_REQUEST, -1, "questions must be a JSON object");

    ollama_json_t *o = ollama_json_object();
    if (!o) return oom(e);
    bool ok = put_str(o, "model", r->model) && put_copy(o, "state", r->state) &&
              put_copy(o, "questions", r->questions) && put_keep_alive(o, r->keep_alive);
    return done(o, ok ? OLLAMA_OK : oom(e), out);
}
