/* Message lists and tool definitions. */
#include "internal.h"

#include <stdlib.h>
#include <string.h>

static bool dup_field(const char **dst, const char *src) {
    if (!src) {
        *dst = NULL;
        return true;
    }
    char *copy = ol_strdup(src);
    *dst = copy;
    return copy != NULL;
}

bool tool_call_copy(ollama_tool_call_t *dst, const ollama_tool_call_t *src) {
    memset(dst, 0, sizeof(*dst));
    if (!dup_field(&dst->function.name, src->function.name)) return false;
    if (src->function.arguments) {
        dst->function.arguments = ollama_json_copy(src->function.arguments);
        if (!dst->function.arguments) {
            tool_call_release(dst);
            return false;
        }
    }
    return true;
}

void tool_call_release(ollama_tool_call_t *t) {
    free((void *)t->function.name);
    ollama_json_free((ollama_json_t *)t->function.arguments);
    memset(t, 0, sizeof(*t));
}

static bool image_copy(ollama_image_t *dst, const ollama_image_t *src) {
    memset(dst, 0, sizeof(*dst));
    if (!dup_field(&dst->path, src->path) || !dup_field(&dst->base64, src->base64)) return false;
    if (src->data) {
        dst->data = ol_memdup(src->data, src->size);
        if (!dst->data) return false;
        dst->size = src->size;
    }
    return true;
}

static void image_release(ollama_image_t *img) {
    free((void *)img->path);
    free((void *)img->base64);
    free((void *)img->data);
}

bool message_copy(ollama_message_t *dst, const ollama_message_t *src) {
    memset(dst, 0, sizeof(*dst));
    bool ok = dup_field(&dst->role, src->role) && dup_field(&dst->content, src->content) &&
              dup_field(&dst->thinking, src->thinking) && dup_field(&dst->tool_name, src->tool_name);
    if (ok && src->images && src->images_count) {
        ollama_image_t *images = calloc(src->images_count, sizeof(*images));
        dst->images = images;
        ok = images != NULL;
        for (size_t i = 0; ok && i < src->images_count; i++) {
            dst->images_count = i + 1;
            ok = image_copy(&images[i], &src->images[i]);
        }
    }
    if (ok && src->tool_calls && src->tool_calls_count) {
        ollama_tool_call_t *calls = calloc(src->tool_calls_count, sizeof(*calls));
        dst->tool_calls = calls;
        ok = calls != NULL;
        for (size_t i = 0; ok && i < src->tool_calls_count; i++) {
            dst->tool_calls_count = i + 1;
            ok = tool_call_copy(&calls[i], &src->tool_calls[i]);
        }
    }
    if (!ok) message_release(dst);
    return ok;
}

void message_release(ollama_message_t *m) {
    free((void *)m->role);
    free((void *)m->content);
    free((void *)m->thinking);
    free((void *)m->tool_name);
    for (size_t i = 0; m->images && i < m->images_count; i++) image_release((ollama_image_t *)&m->images[i]);
    free((void *)m->images);
    for (size_t i = 0; m->tool_calls && i < m->tool_calls_count; i++)
        tool_call_release((ollama_tool_call_t *)&m->tool_calls[i]);
    free((void *)m->tool_calls);
    memset(m, 0, sizeof(*m));
}

/* ---- message lists ---- */

ollama_messages_t *ollama_messages_new(void) { return calloc(1, sizeof(ollama_messages_t)); }

void ollama_messages_free(ollama_messages_t *messages) {
    if (!messages) return;
    for (size_t i = 0; i < messages->count; i++) message_release(&messages->items[i]);
    free(messages->items);
    free(messages);
}

ollama_status_t ollama_messages_add_message(ollama_messages_t *messages, const ollama_message_t *message) {
    if (!messages || !message) return OLLAMA_ERROR_REQUEST;
    if (messages->count == messages->capacity) {
        size_t cap = messages->capacity ? messages->capacity * 2 : 8;
        ollama_message_t *items = realloc(messages->items, cap * sizeof(*items));
        if (!items) return OLLAMA_ERROR_MEMORY;
        messages->items = items;
        messages->capacity = cap;
    }
    if (!message_copy(&messages->items[messages->count], message)) return OLLAMA_ERROR_MEMORY;
    messages->count++;
    return OLLAMA_OK;
}

ollama_status_t ollama_messages_add(ollama_messages_t *messages, const char *role, const char *content) {
    ollama_message_t m;
    memset(&m, 0, sizeof(m));
    m.role = role;
    m.content = content;
    return ollama_messages_add_message(messages, &m);
}

/* ---- tools ---- */

ollama_json_t *ollama_tool_new(const char *name, const char *description) {
    if (!name) return NULL;
    ollama_json_t *tool = ollama_json_object();
    ollama_json_t *fn = NULL, *params = NULL;
    bool ok = ollama_json_set_string(tool, "type", "function") &&
              (fn = ollama_json_set(tool, "function", ollama_json_object())) != NULL &&
              ollama_json_set_string(fn, "name", name) &&
              (!description || ollama_json_set_string(fn, "description", description)) &&
              (params = ollama_json_set(fn, "parameters", ollama_json_object())) != NULL &&
              ollama_json_set_string(params, "type", "object") &&
              ollama_json_set(params, "properties", ollama_json_object()) &&
              ollama_json_set(params, "required", ollama_json_array());
    if (!ok) {
        ollama_json_free(tool);
        return NULL;
    }
    return tool;
}

ollama_json_t *ollama_tool_add_parameter(ollama_json_t *tool, const char *name, const char *type,
                                         const char *description, bool required) {
    /* the tool is owned by the caller, so its members can be modified */
    ollama_json_t *params = (ollama_json_t *)ollama_json_get(ollama_json_get(tool, "function"), "parameters");
    ollama_json_t *props = (ollama_json_t *)ollama_json_get(params, "properties");
    if (!props || !name) return NULL;

    ollama_json_t *prop = ollama_json_set(props, name, ollama_json_object());
    if (!prop) return NULL;
    if (type && !ollama_json_set_string(prop, "type", type)) return NULL;
    if (description && !ollama_json_set_string(prop, "description", description)) return NULL;
    if (required) {
        ollama_json_t *list = (ollama_json_t *)ollama_json_get(params, "required");
        if (ollama_json_type(list) != OLLAMA_JSON_ARRAY) list = ollama_json_set(params, "required", ollama_json_array());
        bool present = false;
        for (size_t i = 0; i < ollama_json_size(list); i++) {
            const char *s = ollama_json_as_string(ollama_json_at(list, i));
            if (s && strcmp(s, name) == 0) present = true;
        }
        if (!list || (!present && !ollama_json_append(list, ollama_json_string(name)))) return NULL;
    }
    return prop;
}
