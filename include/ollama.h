/*
 * ollama-c - C client library for Ollama.
 *
 * A C port of the official Python library (https://github.com/ollama/ollama-python).
 * Every function of the Python `Client` has a C counterpart with the same name:
 *
 *     Python                                   C
 *     ollama.generate(model=..., prompt=...)   ollama_generate(client, &req, &res)
 *     ollama.chat(model=..., messages=...)     ollama_chat(client, &req, &res)
 *     ollama.chat(..., stream=True)            ollama_chat_stream(client, &req, &stream)
 *     ollama.embed / embeddings                ollama_embed / ollama_embeddings
 *     ollama.pull / push / create              ollama_pull / ollama_push / ollama_create
 *     ollama.list / show / ps                  ollama_list / ollama_show / ollama_ps
 *     ollama.copy / delete / create_blob       ollama_copy / ollama_delete / ollama_create_blob
 *     ollama.web_search / web_fetch            ollama_web_search / ollama_web_fetch
 *
 * Conventions:
 *   - Functions return OLLAMA_OK (0) on success or an error code. Details about the
 *     last error are available from ollama_last_error().
 *   - Passing NULL as the client uses a default client, configured from OLLAMA_HOST
 *     like the module-level functions of the Python library (ollama.chat(...)).
 *     The default client is not thread-safe and is released by ollama_cleanup().
 *   - Request structs only borrow the memory they point to.
 *   - Responses are allocated by the library and released with ollama_response_free().
 *     Their strings and arrays live as long as the response itself.
 *   - Optional string fields are NULL when absent. Optional numbers are 0 when absent.
 */
#ifndef OLLAMA_H
#define OLLAMA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OLLAMA_C_VERSION_MAJOR 0
#define OLLAMA_C_VERSION_MINOR 1
#define OLLAMA_C_VERSION_PATCH 0
#define OLLAMA_C_VERSION "0.1.0"

#if defined(__GNUC__) && __GNUC__ >= 4
#define OLLAMA_API __attribute__((visibility("default")))
#else
#define OLLAMA_API
#endif

/* Returns the library version, e.g. "0.1.0". */
OLLAMA_API const char *ollama_version(void);

/* ===========================================================================
 * Errors
 * ======================================================================== */

typedef enum ollama_status {
    OLLAMA_OK = 0,
    OLLAMA_ERROR_REQUEST = 1,    /* invalid arguments (Python: RequestError, ValueError) */
    OLLAMA_ERROR_RESPONSE = 2,   /* the server answered with an error (Python: ResponseError) */
    OLLAMA_ERROR_CONNECTION = 3, /* the server could not be reached (Python: ConnectionError) */
    OLLAMA_ERROR_TRANSPORT = 4,  /* any other network failure: timeout, TLS, ... */
    OLLAMA_ERROR_PARSE = 5,      /* the server sent a response that is not valid JSON */
    OLLAMA_ERROR_IO = 6,         /* a local file (image, blob) could not be read */
    OLLAMA_ERROR_MEMORY = 7      /* out of memory */
} ollama_status_t;

typedef struct ollama_error {
    ollama_status_t code;
    int status_code;     /* HTTP status code for OLLAMA_ERROR_RESPONSE, -1 otherwise */
    const char *message; /* human readable reason, "" on success; never NULL */
} ollama_error_t;

/* Returns a static description of a status code. */
OLLAMA_API const char *ollama_status_string(ollama_status_t status);

/* ===========================================================================
 * JSON values
 *
 * Free-form data (options, format schemas, tool definitions, tool call
 * arguments, model info, ...) is represented as JSON, like the dicts accepted
 * and returned by the Python library.
 * ======================================================================== */

typedef struct ollama_json ollama_json_t;

typedef enum ollama_json_type {
    OLLAMA_JSON_NULL,
    OLLAMA_JSON_BOOL,
    OLLAMA_JSON_NUMBER,
    OLLAMA_JSON_STRING,
    OLLAMA_JSON_ARRAY,
    OLLAMA_JSON_OBJECT
} ollama_json_type_t;

/* Parses a NUL-terminated JSON document. Returns NULL if it is not valid JSON. */
OLLAMA_API ollama_json_t *ollama_json_parse(const char *text);
/* Serializes a value to compact JSON. Release the result with ollama_free(). */
OLLAMA_API char *ollama_json_stringify(const ollama_json_t *value);
/* Returns a deep copy of a value. */
OLLAMA_API ollama_json_t *ollama_json_copy(const ollama_json_t *value);
/* Releases a value created by the application (not one owned by a response). */
OLLAMA_API void ollama_json_free(ollama_json_t *value);

OLLAMA_API ollama_json_t *ollama_json_null(void);
OLLAMA_API ollama_json_t *ollama_json_bool(bool value);
OLLAMA_API ollama_json_t *ollama_json_number(double value);
OLLAMA_API ollama_json_t *ollama_json_int(int64_t value);
OLLAMA_API ollama_json_t *ollama_json_string(const char *value);
OLLAMA_API ollama_json_t *ollama_json_array(void);
OLLAMA_API ollama_json_t *ollama_json_object(void);

/*
 * Adds `value` to an array / sets `key` of an object, replacing any previous
 * value. The container takes ownership of `value` (it is freed on failure).
 * Returns `value`, or NULL on failure.
 */
OLLAMA_API ollama_json_t *ollama_json_append(ollama_json_t *array, ollama_json_t *value);
OLLAMA_API ollama_json_t *ollama_json_set(ollama_json_t *object, const char *key, ollama_json_t *value);
OLLAMA_API ollama_json_t *ollama_json_set_string(ollama_json_t *object, const char *key, const char *value);
OLLAMA_API ollama_json_t *ollama_json_set_number(ollama_json_t *object, const char *key, double value);
OLLAMA_API ollama_json_t *ollama_json_set_int(ollama_json_t *object, const char *key, int64_t value);
OLLAMA_API ollama_json_t *ollama_json_set_bool(ollama_json_t *object, const char *key, bool value);

/* Type of a value; NULL is reported as OLLAMA_JSON_NULL. */
OLLAMA_API ollama_json_type_t ollama_json_type(const ollama_json_t *value);
/* Value lookup in an object, NULL if missing or not an object. */
OLLAMA_API const ollama_json_t *ollama_json_get(const ollama_json_t *object, const char *key);
/* Number of elements of an array or members of an object, 0 otherwise. */
OLLAMA_API size_t ollama_json_size(const ollama_json_t *value);
/* Element `index` of an array, or value of member `index` of an object. */
OLLAMA_API const ollama_json_t *ollama_json_at(const ollama_json_t *value, size_t index);
/* Key of member `index` of an object. */
OLLAMA_API const char *ollama_json_key_at(const ollama_json_t *object, size_t index);

/* Conversions; they return NULL / 0 / false when the value has another type. */
OLLAMA_API const char *ollama_json_as_string(const ollama_json_t *value);
OLLAMA_API double ollama_json_as_number(const ollama_json_t *value);
OLLAMA_API int64_t ollama_json_as_int(const ollama_json_t *value);
OLLAMA_API bool ollama_json_as_bool(const ollama_json_t *value);

/* ===========================================================================
 * Client
 * ======================================================================== */

typedef struct ollama_client ollama_client_t;

/*
 * Creates a client. `host` follows the same rules as the Python library:
 * NULL or "" reads OLLAMA_HOST and falls back to http://127.0.0.1:11434;
 * "example.com", ":8080", "https://example.com/path" and "[::1]:11434" are
 * all accepted. If OLLAMA_API_KEY is set, it is sent as a Bearer token.
 * Returns NULL if out of memory.
 */
OLLAMA_API ollama_client_t *ollama_client_new(const char *host);
OLLAMA_API void ollama_client_free(ollama_client_t *client);

/* Base URL used by the client, e.g. "http://127.0.0.1:11434". */
OLLAMA_API const char *ollama_client_host(const ollama_client_t *client);

/*
 * Adds a header sent with every request (e.g. "Authorization"), replacing a
 * previous one with the same name. A NULL value removes the header.
 * Content-Type, Accept and User-Agent are managed by the library.
 */
OLLAMA_API ollama_status_t ollama_client_set_header(ollama_client_t *client, const char *name,
                                                   const char *value);

/*
 * Network timeout in milliseconds (0, the default, waits forever). Like the
 * httpx timeout of the Python client, it limits the time to connect and the
 * time without receiving data, not the duration of the whole request.
 */
OLLAMA_API void ollama_client_set_timeout(ollama_client_t *client, long timeout_ms);

/* Details of the last error of a client (NULL: the default client). Never NULL. */
OLLAMA_API const ollama_error_t *ollama_last_error(const ollama_client_t *client);

/* Releases the default client used when NULL is passed as client. */
OLLAMA_API void ollama_cleanup(void);

/* ===========================================================================
 * Common types
 * ======================================================================== */

/* An image for multimodal models. Set exactly one of path, data or base64. */
typedef struct ollama_image {
    const char *path;   /* image file, read and base64-encoded by the library */
    const void *data;   /* raw image bytes, base64-encoded by the library */
    size_t size;        /* size of data in bytes */
    const char *base64; /* image already encoded in base64 */
} ollama_image_t;

typedef struct ollama_tool_call {
    struct {
        const char *name;
        const ollama_json_t *arguments; /* JSON object */
    } function;
} ollama_tool_call_t;

/* Chat message, used both in requests and in responses. */
typedef struct ollama_message {
    const char *role;     /* "system", "user", "assistant" or "tool" */
    const char *content;  /* in responses: never NULL ("" if empty) */
    const char *thinking; /* thinking content, for thinking models */
    const ollama_image_t *images;
    size_t images_count;
    const char *tool_name; /* name of the executed tool, for role "tool" */
    const ollama_tool_call_t *tool_calls;
    size_t tool_calls_count;
} ollama_message_t;

typedef struct ollama_token_logprob {
    const char *token;
    double logprob;
} ollama_token_logprob_t;

typedef struct ollama_logprob {
    const char *token;
    double logprob;
    const ollama_token_logprob_t *top_logprobs;
    size_t top_logprobs_count;
} ollama_logprob_t;

typedef struct ollama_model_details {
    const char *parent_model;
    const char *format;
    const char *family;
    const char *const *families;
    size_t families_count;
    const char *parameter_size;
    const char *quantization_level;
} ollama_model_details_t;

/* ===========================================================================
 * Requests
 *
 * Fields left at zero / NULL are not sent, so designated initializers only
 * need to mention what the Python call would pass as keyword arguments:
 *
 *     ollama_generate_request_t req = { .model = "llama3.2", .prompt = "Hi" };
 *
 * Common fields:
 *   options     JSON object of model options, e.g. {"temperature": 0}
 *   format      JSON string "json" or a JSON schema object
 *   keep_alive  duration such as "5m", or a number of seconds such as "0" or "-1"
 *   think       "true", "false", or a level for models that support it
 *               ("low", "medium", "high")
 * ======================================================================== */

typedef struct ollama_generate_request {
    const char *model;
    const char *prompt;
    const char *suffix;
    const char *system;
    const char *template_;
    const int *context; /* context returned by a previous generate call */
    size_t context_count;
    const char *think;
    bool raw;
    bool logprobs;
    int top_logprobs;
    const ollama_json_t *format;
    const ollama_image_t *images;
    size_t images_count;
    const ollama_json_t *options;
    const char *keep_alive;
    int width; /* experimental image generation parameters */
    int height;
    int steps;
} ollama_generate_request_t;

typedef struct ollama_chat_request {
    const char *model;
    const ollama_message_t *messages;
    size_t messages_count;
    const ollama_json_t *tools; /* JSON array of tools, see ollama_tool_new() */
    const char *think;
    bool logprobs;
    int top_logprobs;
    const ollama_json_t *format;
    const ollama_json_t *options;
    const char *keep_alive;
} ollama_chat_request_t;

typedef enum ollama_optional_bool {
    OLLAMA_DEFAULT = 0, /* not sent: the server decides */
    OLLAMA_FALSE,
    OLLAMA_TRUE
} ollama_optional_bool_t;

typedef struct ollama_embed_request {
    const char *model;
    const char *input;         /* a single input ... */
    const char *const *inputs; /* ... or several inputs */
    size_t inputs_count;
    ollama_optional_bool_t truncate;
    const ollama_json_t *options;
    const char *keep_alive;
    int dimensions;
} ollama_embed_request_t;

/* Deprecated in favor of ollama_embed_request_t. */
typedef struct ollama_embeddings_request {
    const char *model;
    const char *prompt;
    const ollama_json_t *options;
    const char *keep_alive;
} ollama_embeddings_request_t;

typedef struct ollama_create_request {
    const char *model;
    const char *quantize;
    const char *from_;
    const ollama_json_t *files;    /* JSON object: file name -> blob digest */
    const ollama_json_t *adapters; /* JSON object: file name -> blob digest */
    const char *template_;
    const char *license;
    const char *system;
    const ollama_json_t *parameters; /* JSON object, like options */
    const ollama_message_t *messages;
    size_t messages_count;
} ollama_create_request_t;

typedef struct ollama_systemone_request {
    const char *model;
    const ollama_json_t *state;     /* text (JSON string), object or array */
    const ollama_json_t *questions; /* JSON object: question name -> question */
    const char *keep_alive;
} ollama_systemone_request_t;

/* ===========================================================================
 * Responses (release with ollama_response_free)
 * ======================================================================== */

typedef struct ollama_generate_response {
    const char *model;
    const char *created_at;
    bool done;
    const char *done_reason;
    int64_t total_duration; /* nanoseconds */
    int64_t load_duration;
    int64_t prompt_eval_count;
    int64_t prompt_eval_duration;
    int64_t eval_count;
    int64_t eval_duration;
    const char *response; /* never NULL; a fragment when streaming */
    const char *thinking;
    const int *context;
    size_t context_count;
    const ollama_logprob_t *logprobs;
    size_t logprobs_count;
    const char *image; /* base64 image, for image generation models */
    int64_t completed; /* image generation progress, when streaming */
    int64_t total;
    const ollama_json_t *json; /* the full response */
} ollama_generate_response_t;

typedef struct ollama_chat_response {
    const char *model;
    const char *created_at;
    bool done;
    const char *done_reason;
    int64_t total_duration;
    int64_t load_duration;
    int64_t prompt_eval_count;
    int64_t prompt_eval_duration;
    int64_t eval_count;
    int64_t eval_duration;
    ollama_message_t message;
    const ollama_logprob_t *logprobs;
    size_t logprobs_count;
    const ollama_json_t *json;
} ollama_chat_response_t;

typedef struct ollama_embedding {
    const double *values;
    size_t size;
} ollama_embedding_t;

typedef struct ollama_embed_response {
    const char *model;
    const char *created_at;
    bool done;
    const char *done_reason;
    int64_t total_duration;
    int64_t load_duration;
    int64_t prompt_eval_count;
    int64_t prompt_eval_duration;
    int64_t eval_count;
    int64_t eval_duration;
    const ollama_embedding_t *embeddings; /* one per input */
    size_t embeddings_count;
    const ollama_json_t *json;
} ollama_embed_response_t;

typedef struct ollama_embeddings_response {
    const double *embedding;
    size_t embedding_size;
    const ollama_json_t *json;
} ollama_embeddings_response_t;

/* Response of pull, push and create. */
typedef struct ollama_progress_response {
    const char *status;
    int64_t completed;
    int64_t total;
    const char *digest;
    const ollama_json_t *json;
} ollama_progress_response_t;

typedef struct ollama_list_model {
    const char *model;
    const char *modified_at;
    const char *digest;
    int64_t size;
    ollama_model_details_t details;
} ollama_list_model_t;

typedef struct ollama_list_response {
    const ollama_list_model_t *models;
    size_t models_count;
    const ollama_json_t *json;
} ollama_list_response_t;

typedef struct ollama_show_response {
    const char *modified_at;
    const char *template_;
    const char *modelfile;
    const char *license;
    ollama_model_details_t details;
    const ollama_json_t *modelinfo; /* "model_info" object */
    const char *parameters;
    const char *const *capabilities;
    size_t capabilities_count;
    const ollama_json_t *json;
} ollama_show_response_t;

typedef struct ollama_process_model {
    const char *model;
    const char *name;
    const char *digest;
    const char *expires_at;
    int64_t size;
    int64_t size_vram;
    ollama_model_details_t details;
    int64_t context_length;
} ollama_process_model_t;

typedef struct ollama_process_response {
    const ollama_process_model_t *models;
    size_t models_count;
    const ollama_json_t *json;
} ollama_process_response_t;

typedef struct ollama_web_search_result {
    const char *content;
    const char *title;
    const char *url;
} ollama_web_search_result_t;

typedef struct ollama_web_search_response {
    const ollama_web_search_result_t *results;
    size_t results_count;
    const ollama_json_t *json;
} ollama_web_search_response_t;

typedef struct ollama_web_fetch_response {
    const char *title;
    const char *content;
    const char *const *links;
    size_t links_count;
    const ollama_json_t *json;
} ollama_web_fetch_response_t;

typedef struct ollama_systemone_answer {
    const char *name;  /* question name */
    const char *type;  /* "noul", "choice" or "score" */
    double noul;       /* noul: probability of true */
    const char *choice; /* choice: selected choice */
    double score;       /* score: expected zero-based score */
    double confidence;  /* choice and score */
    const ollama_json_t *probabilities; /* choice and score: object label -> probability */
    const ollama_json_t *legend;        /* score: object score -> description */
} ollama_systemone_answer_t;

typedef struct ollama_systemone_response {
    const char *model;
    const ollama_systemone_answer_t *answers;
    size_t answers_count;
    struct {
        int64_t input_tokens;
        int64_t output_tokens;
    } usage;
    const ollama_json_t *json;
} ollama_systemone_response_t;

/* Releases any response returned by the functions below. NULL is ignored. */
OLLAMA_API void ollama_response_free(void *response);

/* ===========================================================================
 * API
 * ======================================================================== */

/* Generates a response for a prompt (POST /api/generate). */
OLLAMA_API ollama_status_t ollama_generate(ollama_client_t *client, const ollama_generate_request_t *request,
                                           ollama_generate_response_t **response);

/* Generates a chat response (POST /api/chat). */
OLLAMA_API ollama_status_t ollama_chat(ollama_client_t *client, const ollama_chat_request_t *request,
                                       ollama_chat_response_t **response);

/* Generates embeddings for one or more inputs (POST /api/embed). */
OLLAMA_API ollama_status_t ollama_embed(ollama_client_t *client, const ollama_embed_request_t *request,
                                        ollama_embed_response_t **response);

/* Deprecated in favor of ollama_embed() (POST /api/embeddings). */
OLLAMA_API ollama_status_t ollama_embeddings(ollama_client_t *client, const ollama_embeddings_request_t *request,
                                             ollama_embeddings_response_t **response);

/* Downloads / uploads a model and waits for completion (POST /api/pull, /api/push). */
OLLAMA_API ollama_status_t ollama_pull(ollama_client_t *client, const char *model, bool insecure,
                                       ollama_progress_response_t **response);
OLLAMA_API ollama_status_t ollama_push(ollama_client_t *client, const char *model, bool insecure,
                                       ollama_progress_response_t **response);

/* Creates a model (POST /api/create). */
OLLAMA_API ollama_status_t ollama_create(ollama_client_t *client, const ollama_create_request_t *request,
                                         ollama_progress_response_t **response);

/*
 * Uploads a file as a blob (POST /api/blobs/<digest>) and stores its digest,
 * "sha256:...", in *digest. Release it with ollama_free().
 */
OLLAMA_API ollama_status_t ollama_create_blob(ollama_client_t *client, const char *path, char **digest);

/* Lists local models (GET /api/tags). */
OLLAMA_API ollama_status_t ollama_list(ollama_client_t *client, ollama_list_response_t **response);

/* Deletes a model (DELETE /api/delete). */
OLLAMA_API ollama_status_t ollama_delete(ollama_client_t *client, const char *model);

/* Copies a model (POST /api/copy). */
OLLAMA_API ollama_status_t ollama_copy(ollama_client_t *client, const char *source, const char *destination);

/* Shows information about a model (POST /api/show). */
OLLAMA_API ollama_status_t ollama_show(ollama_client_t *client, const char *model, ollama_show_response_t **response);

/* Lists running models (GET /api/ps). */
OLLAMA_API ollama_status_t ollama_ps(ollama_client_t *client, ollama_process_response_t **response);

/*
 * Searches the web / fetches a page through ollama.com. They require an API key
 * (OLLAMA_API_KEY or an "Authorization: Bearer ..." header).
 * max_results <= 0 uses the default of 3.
 */
OLLAMA_API ollama_status_t ollama_web_search(ollama_client_t *client, const char *query, int max_results,
                                             ollama_web_search_response_t **response);
OLLAMA_API ollama_status_t ollama_web_fetch(ollama_client_t *client, const char *url,
                                            ollama_web_fetch_response_t **response);

/* Scores questions with a local System One model (POST /v1/systemone). */
OLLAMA_API ollama_status_t ollama_systemone(ollama_client_t *client, const ollama_systemone_request_t *request,
                                            ollama_systemone_response_t **response);

/* ===========================================================================
 * Streaming
 *
 * The *_stream functions are the equivalent of stream=True. They return an
 * iterator over the parts of the response:
 *
 *     ollama_stream_t *stream;
 *     if (ollama_chat_stream(client, &req, &stream) == OLLAMA_OK) {
 *         const ollama_chat_response_t *part;
 *         while ((part = ollama_stream_next_chat(stream)) != NULL)
 *             fputs(part->message.content, stdout);
 *         if (ollama_stream_error(stream)->code != OLLAMA_OK) { ... }
 *         ollama_stream_free(stream);
 *     }
 *
 * Errors returned by the server before the first part are reported by the
 * *_stream function itself. A part is valid until the next call on the stream.
 * ======================================================================== */

typedef struct ollama_stream ollama_stream_t;

OLLAMA_API ollama_status_t ollama_generate_stream(ollama_client_t *client, const ollama_generate_request_t *request,
                                                  ollama_stream_t **stream);
OLLAMA_API ollama_status_t ollama_chat_stream(ollama_client_t *client, const ollama_chat_request_t *request,
                                              ollama_stream_t **stream);
OLLAMA_API ollama_status_t ollama_pull_stream(ollama_client_t *client, const char *model, bool insecure,
                                              ollama_stream_t **stream);
OLLAMA_API ollama_status_t ollama_push_stream(ollama_client_t *client, const char *model, bool insecure,
                                              ollama_stream_t **stream);
OLLAMA_API ollama_status_t ollama_create_stream(ollama_client_t *client, const ollama_create_request_t *request,
                                                ollama_stream_t **stream);

/* Next part of the stream, or NULL at the end of the stream or on error. */
OLLAMA_API const ollama_generate_response_t *ollama_stream_next_generate(ollama_stream_t *stream);
OLLAMA_API const ollama_chat_response_t *ollama_stream_next_chat(ollama_stream_t *stream);
OLLAMA_API const ollama_progress_response_t *ollama_stream_next_progress(ollama_stream_t *stream);

/* Error that ended the stream; code is OLLAMA_OK if there was none. Never NULL. */
OLLAMA_API const ollama_error_t *ollama_stream_error(const ollama_stream_t *stream);

/*
 * For chat streams: the message assembled from all the parts read so far
 * (content, thinking and tool calls). Handy to append the answer to the
 * history. NULL for other streams.
 */
OLLAMA_API const ollama_message_t *ollama_stream_message(const ollama_stream_t *stream);

/* Closes the stream, aborting the request if it is still running. */
OLLAMA_API void ollama_stream_free(ollama_stream_t *stream);

/* ===========================================================================
 * Helpers
 * ======================================================================== */

/* A growable list of messages that owns copies of them, for chat histories. */
typedef struct ollama_messages {
    ollama_message_t *items;
    size_t count;
    size_t capacity;
} ollama_messages_t;

OLLAMA_API ollama_messages_t *ollama_messages_new(void);
OLLAMA_API void ollama_messages_free(ollama_messages_t *messages);
/* Appends a copy of a message with the given role and content. */
OLLAMA_API ollama_status_t ollama_messages_add(ollama_messages_t *messages, const char *role, const char *content);
/* Appends a deep copy of a message (e.g. &response->message). */
OLLAMA_API ollama_status_t ollama_messages_add_message(ollama_messages_t *messages, const ollama_message_t *message);

/*
 * Builds a function tool definition:
 *   {"type": "function", "function": {"name": ..., "description": ...,
 *    "parameters": {"type": "object", "properties": {}, "required": []}}}
 * This is the C replacement for passing Python functions as tools.
 */
OLLAMA_API ollama_json_t *ollama_tool_new(const char *name, const char *description);
/*
 * Adds a parameter to a tool built with ollama_tool_new(). `type` is a JSON
 * schema type ("string", "integer", "number", "boolean", "array", "object").
 * Returns the parameter's schema object (owned by the tool), so that more
 * keywords such as "enum" can be added, or NULL on failure.
 */
OLLAMA_API ollama_json_t *ollama_tool_add_parameter(ollama_json_t *tool, const char *name, const char *type,
                                                    const char *description, bool required);

/* Base64 helpers, e.g. for images returned by image generation models. */
OLLAMA_API char *ollama_base64_encode(const void *data, size_t size);
OLLAMA_API unsigned char *ollama_base64_decode(const char *text, size_t *size);

/* Releases memory returned by the library (strings, decoded data). */
OLLAMA_API void ollama_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* OLLAMA_H */
