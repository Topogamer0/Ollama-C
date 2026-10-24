/**
 * @file ollama.h
 * @brief Ollama-C: C client library for Ollama, a port of ollama-python.
 *
 * A C port of the official Python library (https://github.com/ollama/ollama-python).
 * Every function of the Python `Client` has a C counterpart with the same name:
 *
 * | Python                                    | C                                                   |
 * | ----------------------------------------- | --------------------------------------------------- |
 * | `ollama.generate(model=..., prompt=...)`  | `ollama_generate(client, &req, &res)`               |
 * | `ollama.chat(model=..., messages=...)`    | `ollama_chat(client, &req, &res)`                   |
 * | `ollama.chat(..., stream=True)`           | `ollama_chat_stream(client, &req, &stream)`         |
 * | `ollama.embed` / `embeddings`             | `ollama_embed` / `ollama_embeddings`                |
 * | `ollama.pull` / `push` / `create`         | `ollama_pull` / `ollama_push` / `ollama_create`     |
 * | `ollama.list` / `show` / `ps`             | `ollama_list` / `ollama_show` / `ollama_ps`         |
 * | `ollama.copy` / `delete` / `create_blob`  | `ollama_copy` / `ollama_delete` / `ollama_create_blob` |
 * | `ollama.web_search` / `web_fetch`         | `ollama_web_search` / `ollama_web_fetch`            |
 * | `ollama.systemone`                        | `ollama_systemone`                                  |
 *
 * Conventions:
 * - Functions that return an #ollama_status_t return #OLLAMA_OK (0) on success or an error code
 *   (see @ref errors); for the calls that take a client, details about the last error are
 *   available from ollama_last_error(). Functions that return a pointer report a failure with
 *   NULL; those that take neither a client nor a stream (ollama_client_new(), the JSON functions,
 *   the helpers) record no error details.
 * - Passing NULL as the client uses a default client, configured from OLLAMA_HOST like the
 *   module-level functions of the Python library (`ollama.chat(...)`). The default client is not
 *   thread-safe and is released by ollama_cleanup() (see @ref client).
 * - Request structs only borrow the memory they point to (see @ref requests).
 * - Responses are allocated by the library and released with ollama_response_free().
 *   Their strings and arrays live as long as the response itself (see @ref responses).
 * - Optional string fields are NULL when absent. Optional numbers are 0 when absent.
 * - Strings are NUL-terminated UTF-8; the library does not validate the encoding. File paths are
 *   the exception: they are passed to fopen() unchanged, so on Windows they are in the active code
 *   page, like the arguments of main().
 */
#ifndef OLLAMA_H
#define OLLAMA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup version Version and export macro
 * @brief Version of the header and of the compiled library.
 * @{
 */

/** @brief Major version number of this header. */
#define OLLAMA_C_VERSION_MAJOR 0
/** @brief Minor version number of this header. */
#define OLLAMA_C_VERSION_MINOR 2
/** @brief Patch version number of this header. */
#define OLLAMA_C_VERSION_PATCH 1
/**
 * @brief Version of this header as a string, "MAJOR.MINOR.PATCH".
 *
 * Compare it with ollama_version() to detect an application compiled against a different header
 * than the library it runs with.
 */
#define OLLAMA_C_VERSION "0.2.1"

/**
 * @def OLLAMA_API
 * @brief Marks the functions exported by the library.
 *
 * It expands to `__declspec(dllexport)` on Windows while the library itself is built as a DLL
 * (the build defines `OLLAMA_BUILDING_DLL`), otherwise to `__attribute__((visibility("default")))`
 * with GCC 4 or later and compatible compilers such as Clang, and to nothing with other compilers.
 * Applications do not need to define anything.
 */
#if defined(_WIN32) && defined(OLLAMA_BUILDING_DLL)
#define OLLAMA_API __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
#define OLLAMA_API __attribute__((visibility("default")))
#else
#define OLLAMA_API
#endif

/**
 * @brief Returns the version of the compiled library, such as "0.2.0".
 *
 * It can differ from #OLLAMA_C_VERSION, the version of the header the application was compiled
 * with, when the application runs with another build of a shared library.
 *
 * @return A static NUL-terminated string; do not free it.
 */
OLLAMA_API const char *ollama_version(void);

/** @} */

/**
 * @defgroup errors Errors
 * @brief Status codes and error details.
 *
 * The functions that return an #ollama_status_t, such as all the API calls, return #OLLAMA_OK (0)
 * on success, or the code of the error. The functions that return a pointer report a failure with
 * NULL; those that take neither a client nor a stream (ollama_client_new(), the JSON functions,
 * the helpers) record no error details. Where ollama-python raises an exception, Ollama-C returns
 * the matching code and keeps the details (message and HTTP status code) in an #ollama_error_t:
 * - ollama_last_error() for the calls that take a client, including the errors of the `*_stream`
 *   functions themselves;
 * - ollama_stream_error() for the errors that end a stream while its parts are read.
 *
 * @code{.c}
 * if (ollama_chat(client, &req, &res) != OLLAMA_OK) {
 *     const ollama_error_t *err = ollama_last_error(client);
 *     if (err->code == OLLAMA_ERROR_RESPONSE && err->status_code == 404)
 *         fprintf(stderr, "model not found: %s\n", err->message);
 * }
 * @endcode
 * @{
 */

/** @brief Result of a call: #OLLAMA_OK or the code of the error. */
typedef enum ollama_status {
    OLLAMA_OK = 0, ///< Success.
    /**
     * Invalid arguments or settings, detected by the library (Python: RequestError, ValueError).
     * For example: a NULL request, argument or output pointer, a missing model, a message without
     * role, an image with neither path, data nor base64, an array count without its array, an
     * invalid header, a missing API key for web search, an invalid OLLAMA_HOST or OLLAMA_API_KEY when
     * the default client is created, or the wrong `ollama_stream_next_*()` function for a stream.
     * Each function lists its own cases.
     */
    OLLAMA_ERROR_REQUEST = 1,
    /**
     * The server answered with an HTTP error status, or sent an error in the middle of a stream
     * (Python: ResponseError). The message is the server's error text (see ollama_error_t::message).
     */
    OLLAMA_ERROR_RESPONSE = 2,
    /**
     * The server could not be reached: connection refused, host or proxy name not resolved
     * (Python: ConnectionError).
     */
    OLLAMA_ERROR_CONNECTION = 3,
    /** Any other network failure: timeout, TLS error, interrupted transfer, too many redirects. */
    OLLAMA_ERROR_TRANSPORT = 4,
    OLLAMA_ERROR_PARSE = 5,      ///< The server sent an answer, or a line of a stream, that is not valid JSON.
    OLLAMA_ERROR_IO = 6,         ///< A local file (image, blob) could not be read.
    /**
     * Out of memory. Also reported when the default client cannot be created for another reason
     * than an invalid OLLAMA_HOST or OLLAMA_API_KEY (#OLLAMA_ERROR_REQUEST), such as a failure to
     * initialize libcurl.
     */
    OLLAMA_ERROR_MEMORY = 7
} ollama_status_t;

/**
 * @brief Details of an error (Python: the attributes of the exception).
 *
 * Returned by ollama_last_error() and ollama_stream_error(). The struct belongs to the client or
 * stream it comes from: copy what you need before the next call on it.
 */
typedef struct ollama_error {
    ollama_status_t code; ///< The status code; #OLLAMA_OK when there was no error.
    /**
     * HTTP status code of the error answer for #OLLAMA_ERROR_RESPONSE (Python:
     * ResponseError.status_code); -1 for the other codes and for errors sent inside a stream.
     */
    int status_code;
    /**
     * Human readable reason, never NULL: "" on success. For #OLLAMA_ERROR_RESPONSE it is the
     * "error" string of the server's JSON answer (Python: ResponseError.error); otherwise,
     * including JSON without a string "error" member, the raw answer (up to its first NUL byte);
     * or "HTTP " followed by the status code when the answer is empty.
     */
    const char *message;
} ollama_error_t;

/**
 * @brief Returns a static English description of a status code, such as "connection error".
 *
 * @param[in] status the status code; values outside #ollama_status_t give "unknown error"
 * @return A static NUL-terminated string, never NULL; do not free it.
 */
OLLAMA_API const char *ollama_status_string(ollama_status_t status);

/** @} */

/**
 * @defgroup json_values JSON values
 * @brief A small JSON API for free-form data.
 *
 * Free-form data (options, format schemas, tool definitions, tool call arguments, model info,
 * ...) is represented as JSON, like the dicts accepted and returned by the Python library.
 *
 * Ownership rules:
 * - Values created by the application (the constructors, ollama_json_parse(), ollama_json_copy(),
 *   ollama_tool_new()) are released with ollama_json_free(), unless they have been inserted into
 *   a container.
 * - ollama_json_append() and ollama_json_set() take ownership of the value they insert, even
 *   when they fail.
 * - The accessors return pointers into the value they are called on: they stay valid until that
 *   value is freed or the member is replaced.
 * - Values reachable from a response (`json` fields, tool call arguments, ...) belong to the
 *   response: never free them, and copy them with ollama_json_copy() to keep them longer.
 * - Every function accepts NULL where a value is expected and handles it as described below,
 *   so calls can be chained without checking each result.
 *
 * Numbers keep 64-bit integers exact: integers parsed from text or created with ollama_json_int()
 * are stored both as int64_t and as double.
 *
 * @code{.c}
 * ollama_json_t *options = ollama_json_object();
 * ollama_json_set_number(options, "temperature", 0.2);
 * ollama_json_set_int(options, "num_ctx", 8192);
 * ollama_chat_request_t req = {.model = "gemma3", .messages = msgs, .messages_count = n, .options = options};
 * // ... ollama_chat(client, &req, &res) ...
 * ollama_json_free(options);
 * @endcode
 * @{
 */

/** @brief Opaque JSON value: null, boolean, number, string, array or object. */
typedef struct ollama_json ollama_json_t;

/** @brief Type of a JSON value, as returned by ollama_json_type(). */
typedef enum ollama_json_type {
    OLLAMA_JSON_NULL,   ///< null; also the type reported for a NULL pointer.
    OLLAMA_JSON_BOOL,   ///< true or false; read it with ollama_json_as_bool().
    /** A number, integer or floating point; read it with ollama_json_as_number() or ollama_json_as_int(). */
    OLLAMA_JSON_NUMBER,
    OLLAMA_JSON_STRING, ///< A string; read it with ollama_json_as_string().
    OLLAMA_JSON_ARRAY,  ///< An array; iterate over it with ollama_json_size() and ollama_json_at().
    /**
     * An object; its members keep their order. Read a member with ollama_json_get(), or iterate
     * with ollama_json_size(), ollama_json_key_at() and ollama_json_at().
     */
    OLLAMA_JSON_OBJECT
} ollama_json_type_t;

/**
 * @brief Parses a NUL-terminated JSON document.
 *
 * The whole text must be a single JSON value, optionally surrounded by whitespace. Integers that
 * fit in 64 bits are kept exact; other numbers are converted to double, whatever the C locale.
 * Duplicate object keys are kept (ollama_json_size() counts them) and ollama_json_get() returns
 * the last one. Escaped lone surrogates become U+FFFD. Values nested more than 512 deep are
 * rejected; the innermost value counts, so at most 511 levels of non-empty arrays or objects
 * (512 when the innermost one is empty).
 *
 * @param[in] text the JSON text, or NULL
 * @return A new value to release with ollama_json_free(), or NULL if @p text is NULL or not valid
 *         JSON, or if memory runs out.
 */
OLLAMA_API ollama_json_t *ollama_json_parse(const char *text);
/**
 * @brief Serializes a value to compact JSON text.
 *
 * No whitespace is added and object members keep their order. Integers are written exactly, other
 * numbers with up to 17 significant digits (enough to read the same double back) and '.' as
 * decimal point whatever the C locale; NaN and infinities become null. Quotes, backslashes and
 * control characters are escaped; other bytes, such as UTF-8 sequences, are copied as they are.
 *
 * @param[in] value the value; NULL is serialized as "null"
 * @return A new NUL-terminated string to release with ollama_free(), or NULL if memory runs out.
 */
OLLAMA_API char *ollama_json_stringify(const ollama_json_t *value);
/**
 * @brief Returns a deep copy of a value.
 *
 * Use it to keep a value that belongs to a response (such as tool call arguments) after
 * ollama_response_free(), or to get a modifiable copy of it.
 *
 * @param[in] value the value to copy, or NULL
 * @return A new value to release with ollama_json_free(), or NULL if @p value is NULL or memory
 *         runs out.
 */
OLLAMA_API ollama_json_t *ollama_json_copy(const ollama_json_t *value);
/**
 * @brief Releases a value created by the application, with everything it contains.
 *
 * Only for the values owned by the application: those returned by the constructors,
 * ollama_json_parse(), ollama_json_copy() and ollama_tool_new(). Never free a value that has been
 * inserted into a container (the container frees it) or that belongs to a response.
 *
 * @param[in] value the value to release; NULL is ignored
 */
OLLAMA_API void ollama_json_free(ollama_json_t *value);

/**
 * @brief Creates a JSON null.
 * @return A new value to release with ollama_json_free() or to insert into a container, or NULL
 *         if memory runs out.
 */
OLLAMA_API ollama_json_t *ollama_json_null(void);
/**
 * @brief Creates a JSON boolean.
 * @param[in] value the boolean
 * @return A new value to release with ollama_json_free() or to insert into a container, or NULL
 *         if memory runs out.
 */
OLLAMA_API ollama_json_t *ollama_json_bool(bool value);
/**
 * @brief Creates a floating-point number.
 *
 * It is serialized with up to 17 significant digits, enough to read back the same double (3.0 is
 * written 3). Use ollama_json_int() for integers that must stay exact beyond 2^53.
 *
 * @param[in] value the number
 * @return A new value to release with ollama_json_free() or to insert into a container, or NULL
 *         if memory runs out.
 */
OLLAMA_API ollama_json_t *ollama_json_number(double value);
/**
 * @brief Creates an integer number, stored and serialized exactly.
 * @param[in] value the integer
 * @return A new value to release with ollama_json_free() or to insert into a container, or NULL
 *         if memory runs out.
 */
OLLAMA_API ollama_json_t *ollama_json_int(int64_t value);
/**
 * @brief Creates a string.
 * @param[in] value the NUL-terminated text, copied
 * @return A new value to release with ollama_json_free() or to insert into a container, or NULL
 *         if @p value is NULL or memory runs out.
 */
OLLAMA_API ollama_json_t *ollama_json_string(const char *value);
/**
 * @brief Creates an empty array; fill it with ollama_json_append().
 * @return A new value to release with ollama_json_free() or to insert into a container, or NULL
 *         if memory runs out.
 */
OLLAMA_API ollama_json_t *ollama_json_array(void);
/**
 * @brief Creates an empty object; fill it with ollama_json_set() and its variants.
 * @return A new value to release with ollama_json_free() or to insert into a container, or NULL
 *         if memory runs out.
 */
OLLAMA_API ollama_json_t *ollama_json_object(void);

/**
 * @brief Appends a value to an array.
 *
 * The array takes ownership of @p value, even on failure: then @p value is freed. The returned
 * pointer can be used to fill a container just appended:
 *
 * @code{.c}
 * ollama_json_t *item = ollama_json_append(array, ollama_json_object());
 * ollama_json_set_string(item, "name", "value"); // harmless if item is NULL
 * @endcode
 *
 * @param[in,out] array the array; any other value, or NULL, makes the call fail
 * @param[in] value the value to append, or NULL (such as the result of a failed constructor)
 * @return @p value, now owned by @p array, or NULL on failure: @p value is NULL, @p array is not an
 *         array, or memory runs out.
 */
OLLAMA_API ollama_json_t *ollama_json_append(ollama_json_t *array, ollama_json_t *value);
/**
 * @brief Sets member @p key of an object, replacing and freeing the previous value of that key.
 *
 * A new key is added after the existing members; a replaced member keeps its position. Like
 * ollama_json_append(), the object takes ownership of @p value even on failure.
 *
 * @param[in,out] object the object; any other value, or NULL, makes the call fail
 * @param[in] key the member name, copied
 * @param[in] value the new value, or NULL (such as the result of a failed constructor)
 * @return @p value, now owned by @p object, or NULL on failure: @p value or @p key is NULL,
 *         @p object is not an object, or memory runs out.
 */
OLLAMA_API ollama_json_t *ollama_json_set(ollama_json_t *object, const char *key, ollama_json_t *value);
/**
 * @brief Sets member @p key of an object to a string: ollama_json_set() with ollama_json_string().
 * @param[in,out] object the object
 * @param[in] key the member name, copied
 * @param[in] value the text, copied; NULL sets nothing and makes the call fail
 * @return The new string value, owned by @p object, or NULL on failure.
 */
OLLAMA_API ollama_json_t *ollama_json_set_string(ollama_json_t *object, const char *key, const char *value);
/**
 * @brief Sets member @p key of an object to a number: ollama_json_set() with ollama_json_number().
 * @param[in,out] object the object
 * @param[in] key the member name, copied
 * @param[in] value the number
 * @return The new number value, owned by @p object, or NULL on failure.
 */
OLLAMA_API ollama_json_t *ollama_json_set_number(ollama_json_t *object, const char *key, double value);
/**
 * @brief Sets member @p key of an object to an integer: ollama_json_set() with ollama_json_int().
 * @param[in,out] object the object
 * @param[in] key the member name, copied
 * @param[in] value the integer
 * @return The new number value, owned by @p object, or NULL on failure.
 */
OLLAMA_API ollama_json_t *ollama_json_set_int(ollama_json_t *object, const char *key, int64_t value);
/**
 * @brief Sets member @p key of an object to a boolean: ollama_json_set() with ollama_json_bool().
 * @param[in,out] object the object
 * @param[in] key the member name, copied
 * @param[in] value the boolean
 * @return The new boolean value, owned by @p object, or NULL on failure.
 */
OLLAMA_API ollama_json_t *ollama_json_set_bool(ollama_json_t *object, const char *key, bool value);

/**
 * @brief Returns the type of a value.
 * @param[in] value the value, or NULL
 * @return The type; #OLLAMA_JSON_NULL for a JSON null and for a NULL pointer.
 */
OLLAMA_API ollama_json_type_t ollama_json_type(const ollama_json_t *value);
/**
 * @brief Looks up a member of an object (Python: `dict.get()`).
 * @param[in] object the object
 * @param[in] key the member name
 * @return The value of the member, owned by @p object (the last one if the key appears several
 *         times), or NULL if @p object is NULL or not an object, @p key is NULL, or the key is
 *         missing.
 */
OLLAMA_API const ollama_json_t *ollama_json_get(const ollama_json_t *object, const char *key);
/**
 * @brief Returns the number of elements of an array or of members of an object.
 * @param[in] value the value
 * @return The size, or 0 for the other types and for NULL.
 */
OLLAMA_API size_t ollama_json_size(const ollama_json_t *value);
/**
 * @brief Returns element @p index of an array, or the value of member @p index of an object.
 *
 * Together with ollama_json_size() and ollama_json_key_at(), it iterates over a container:
 *
 * @code{.c}
 * for (size_t i = 0; i < ollama_json_size(object); i++) {
 *     const char *key = ollama_json_key_at(object, i);       // NULL for arrays
 *     const ollama_json_t *value = ollama_json_at(object, i);
 *     // ...
 * }
 * @endcode
 *
 * @param[in] value the array or object
 * @param[in] index the position, starting at 0
 * @return The element, owned by @p value, or NULL if @p index is out of range or @p value is not
 *         an array or an object.
 */
OLLAMA_API const ollama_json_t *ollama_json_at(const ollama_json_t *value, size_t index);
/**
 * @brief Returns the key of member @p index of an object.
 * @param[in] object the object
 * @param[in] index the position of the member, starting at 0
 * @return The key, owned by @p object, or NULL if @p index is out of range or @p object is not an
 *         object.
 */
OLLAMA_API const char *ollama_json_key_at(const ollama_json_t *object, size_t index);

/**
 * @brief Returns the text of a string value.
 * @param[in] value the value
 * @return The NUL-terminated text, owned by @p value (a string with an escaped NUL character
 *         appears cut at that point), or NULL if @p value is NULL or not a string.
 */
OLLAMA_API const char *ollama_json_as_string(const ollama_json_t *value);
/**
 * @brief Returns the value of a number as a double.
 * @param[in] value the value
 * @return The number (integers beyond 2^53 may be rounded), or 0.0 if @p value is NULL or not a
 *         number.
 */
OLLAMA_API double ollama_json_as_number(const ollama_json_t *value);
/**
 * @brief Returns the value of a number as a 64-bit integer.
 * @param[in] value the value
 * @return The integer: exact for integers, truncated toward zero for other numbers, and 0 for
 *         numbers beyond about +/-9.2e18; 0 if @p value is NULL or not a number.
 */
OLLAMA_API int64_t ollama_json_as_int(const ollama_json_t *value);
/**
 * @brief Returns the value of a boolean.
 * @param[in] value the value
 * @return true only for the JSON value true; false for false, for the other types and for NULL
 *         (there is no truthiness conversion).
 */
OLLAMA_API bool ollama_json_as_bool(const ollama_json_t *value);

/** @} */

/**
 * @defgroup client Client
 * @brief Connection settings, the default client and error reporting.
 *
 * A client (Python: `ollama.Client`) holds the base URL of the server, the headers, the timeout
 * and a reusable libcurl handle. Every API function takes a client as first argument; NULL
 * selects the default client, the equivalent of the module-level functions of the Python library
 * (`ollama.chat(...)`): it is created on first use with `ollama_client_new(NULL)`, so it reads
 * OLLAMA_HOST and OLLAMA_API_KEY, and it is released by ollama_cleanup(). While it cannot be
 * created, every call that needs it tries again and fails (see ollama_last_error()).
 *
 * Threads: a client must not be used by several threads at the same time, and the default client
 * is not thread-safe at all: create one client per thread instead. The global initialization of
 * libcurl is not thread-safe either, so multi-threaded programs should call
 * `curl_global_init(CURL_GLOBAL_DEFAULT)` once at startup, before creating clients.
 *
 * @code{.c}
 * ollama_client_t *client = ollama_client_new("http://localhost:11434");
 * ollama_client_set_header(client, "x-some-header", "some-value");
 * ollama_client_set_timeout(client, 30000);
 * // ... API calls with client ...
 * ollama_client_free(client);
 * @endcode
 * @{
 */

/** @brief Opaque client, created with ollama_client_new() and released with ollama_client_free(). */
typedef struct ollama_client ollama_client_t;

/**
 * @brief Creates a client (Python: `ollama.Client(host=...)`).
 *
 * @p host follows the rules of the Python library. When it is NULL or "", the OLLAMA_HOST
 * environment variable is used instead, and when that is unset or empty too, the local server.
 * The scheme defaults to http; the port defaults to 11434, or to 80 / 443 when an explicit
 * `http://` / `https://` scheme is given. Tabs and newlines are ignored, the host name is lowercased
 * (up to the zone of a scoped IPv6 address), user information, query and fragment are dropped, and
 * a path is kept without its leading and trailing slashes. Like current Python versions (3.11+),
 * which raise ValueError, the client is not created when the host has unbalanced brackets, data
 * around the brackets, an invalid address between brackets, or a port that is not a number up to
 * 65535. IPvFuture literals (`[v1.x]`) must use the characters allowed by RFC 3986.
 *
 * Unlike the Python library, and like the Ollama command-line client, an unspecified address
 * (`0.0.0.0` or `::`, with or without scheme, port and path) is replaced by the loopback address
 * of the same family (`127.0.0.1` or `::1`): OLLAMA_HOST=0.0.0.0 makes the server listen on
 * every interface, and connecting to 0.0.0.0 does not work everywhere (it fails on Windows). For
 * the same reason a bare `::` is accepted like `[::]`. For example:
 *
 * | `host`                                   | base URL                      |
 * | ---------------------------------------- | ----------------------------- |
 * | NULL or "" (and OLLAMA_HOST unset or "") | `http://127.0.0.1:11434`      |
 * | `"example.com"`                          | `http://example.com:11434`    |
 * | `":8080"`                                | `http://127.0.0.1:8080`       |
 * | `"http://example.com"`                   | `http://example.com:80`       |
 * | `"https://example.com/api/"`             | `https://example.com:443/api` |
 * | `"[::1]:11434"`                          | `http://[::1]:11434`          |
 * | `"0.0.0.0"`                              | `http://127.0.0.1:11434`      |
 * | `"https://0.0.0.0:4321"`                 | `https://127.0.0.1:4321`      |
 * | `"[::]:8080"`                            | `http://[::1]:8080`           |
 * | `"::"`                                   | `http://[::1]:11434`          |
 *
 * If the OLLAMA_API_KEY environment variable is set and not empty, it is sent with every request
 * in an `Authorization: Bearer ...` header, which ollama_client_set_header() can replace or remove.
 *
 * @param[in] host the address of the server, copied, or NULL to use OLLAMA_HOST
 * @return A new client to release with ollama_client_free(), or NULL, with no error details, if
 *         the host (@p host, or OLLAMA_HOST) is invalid as described above, if OLLAMA_API_KEY
 *         contains CR or LF, if memory runs out or if libcurl cannot be initialized.
 */
OLLAMA_API ollama_client_t *ollama_client_new(const char *host);
/**
 * @brief Releases a client (Python: `Client.close()`).
 *
 * Responses and streams obtained through the client remain valid: they do not reference it. The
 * pointers returned by ollama_client_host() and ollama_last_error() for this client become invalid.
 *
 * @param[in] client the client to release; NULL is ignored
 */
OLLAMA_API void ollama_client_free(ollama_client_t *client);

/**
 * @brief Returns the base URL used by a client, such as "http://127.0.0.1:11434".
 *
 * It always contains the scheme and the port (see ollama_client_new()).
 *
 * @param[in] client the client, or NULL for the default client (created if needed)
 * @return The URL, owned by the client and valid until it is released; NULL only when the default
 *         client cannot be created.
 */
OLLAMA_API const char *ollama_client_host(const ollama_client_t *client);

/**
 * @brief Sets a header sent with every request (Python: the `headers` argument of `Client`).
 *
 * Header names are case-insensitive: a header with the same name is replaced. A NULL @p value
 * removes the header, and "" sends it with an empty value. Setting "Authorization" replaces the
 * header created from OLLAMA_API_KEY. Content-Type, Accept and User-Agent are always set by the
 * library: values given for them are stored but not sent.
 *
 * Unlike the API calls, this function does not clear the last error of the client when it
 * succeeds.
 *
 * @param[in,out] client the client, or NULL for the default client (created if needed)
 * @param[in] name the header name, copied; it must not be empty or contain ':', CR or LF
 * @param[in] value the header value, copied; it must not contain CR or LF. NULL removes the header.
 * @return #OLLAMA_OK or an error code:
 * @retval OLLAMA_OK the header was set or removed (removing a missing header is not an error)
 * @retval OLLAMA_ERROR_REQUEST @p name or @p value is invalid (the last error says "invalid
 *         header"), or @p client is NULL and the default client cannot be created because
 *         OLLAMA_HOST or OLLAMA_API_KEY is invalid
 * @retval OLLAMA_ERROR_MEMORY out of memory, or @p client is NULL and the default client cannot be
 *         created for another reason
 */
OLLAMA_API ollama_status_t ollama_client_set_header(ollama_client_t *client, const char *name,
                                                   const char *value);

/**
 * @brief Sets the network timeout of a client (Python: the `timeout` argument of `Client`).
 *
 * Like the httpx timeout of the Python client, it limits the time to connect and the time without
 * receiving any data, not the duration of the whole request; the second limit is applied in whole
 * seconds, rounded up. A blocking call receives nothing until the server is done, so a blocking
 * generation, chat, pull, push or create fails with #OLLAMA_ERROR_TRANSPORT when it takes longer
 * than the timeout: use the streaming functions in that case.
 * A call that times out fails with #OLLAMA_ERROR_TRANSPORT. The timeout applies to the requests and
 * streams started after this call.
 *
 * @param[in,out] client the client, or NULL for the default client (created if needed; nothing
 *                happens if it cannot be)
 * @param[in] timeout_ms the timeout in milliseconds. 0 (the default) or a negative value sets no
 *            limit, apart from the 300 second connect timeout built into libcurl.
 */
OLLAMA_API void ollama_client_set_timeout(ollama_client_t *client, long timeout_ms);

/**
 * @brief Returns the details of the last error of a client.
 *
 * Every API call (the functions of @ref api and the `*_stream` functions of @ref streaming)
 * clears the error when it starts, so after a successful call the code is #OLLAMA_OK and the
 * message "". The errors that happen while the parts of a stream are read are reported by
 * ollama_stream_error() instead.
 *
 * @param[in] client the client, or NULL for the default client. NULL does not create the default
 *            client: before its first use the result is a static record with #OLLAMA_OK, and when it
 *            could not be created, a static record with #OLLAMA_ERROR_REQUEST ("invalid
 *            OLLAMA_HOST or OLLAMA_API_KEY") or #OLLAMA_ERROR_MEMORY ("out of memory").
 * @return The error, never NULL. It belongs to the client: the pointer is valid until the client is
 *         released, its message until the next call that uses the client.
 */
OLLAMA_API const ollama_error_t *ollama_last_error(const ollama_client_t *client);

/**
 * @brief Releases the default client, used when NULL is passed as client.
 *
 * Call it before exiting, or to make the next call create a new default client, for example after
 * changing OLLAMA_HOST. It is harmless when there is no default client. The pointers returned by
 * ollama_client_host() and ollama_last_error() for the default client become invalid. It does not
 * call `curl_global_cleanup()`.
 */
OLLAMA_API void ollama_cleanup(void);

/** @} */

/**
 * @defgroup common_types Common types
 * @brief Types used both in requests and in responses.
 *
 * In requests the application owns the memory these structs point to; in responses the library
 * owns it, until ollama_response_free() (or, for a stream, until its next part).
 * @{
 */

/**
 * @brief An image for multimodal models (Python: the items of `images=[...]`).
 *
 * Set exactly one of `path`, `data` or `base64`; if several are set, `base64` wins over `path`,
 * which wins over `data`. The file is read, and the data encoded, when the request is sent. Images
 * only appear in requests: messages of responses never carry images.
 *
 * @code{.c}
 * ollama_image_t images[] = {{.path = "cat.jpg"}, {.data = bytes, .size = n}, {.base64 = encoded}};
 * ollama_message_t msg = {.role = "user", .content = "Compare them", .images = images, .images_count = 3};
 * @endcode
 */
typedef struct ollama_image {
    /** Path of an image file, read and base64-encoded by the library (#OLLAMA_ERROR_IO if it cannot be
     *  read). It is passed to fopen(): on Windows it is in the active code page. */
    const char *path;
    const void *data;   ///< Raw image bytes, base64-encoded by the library.
    size_t size;        ///< Size of `data` in bytes.
    const char *base64; ///< Image already encoded in base64, sent as it is.
} ollama_image_t;

/**
 * @brief A tool call requested by the model (Python: `Message.ToolCall`).
 *
 * Responses fill it from the answer of the model. In requests it sends back the calls of an
 * assistant message of the history, a NULL name being left out and NULL arguments sent as an
 * empty object.
 */
typedef struct ollama_tool_call {
    /** The function to call (Python: `ToolCall.function`). */
    struct {
        const char *name;               ///< Name of the function; NULL if missing from the answer.
        /**
         * Arguments, normally a JSON object read with ollama_json_get(). Its type is not checked,
         * either in responses (the "arguments" value of the answer as it is, NULL if missing) or
         * in requests (sent as it is). In responses it belongs to the response.
         */
        const ollama_json_t *arguments;
    } function;
} ollama_tool_call_t;

/**
 * @brief A chat message, used both in requests and in responses (Python: `ollama.Message`).
 *
 * In requests only `role` is required, and NULL or empty strings are not sent, like
 * `_copy_messages()` in the Python client. In responses `content` is never NULL, the other
 * strings are NULL when absent and `images` is always NULL.
 *
 * @code{.c}
 * ollama_message_t messages[] = {
 *     {.role = "system", .content = "Answer in one sentence."},
 *     {.role = "user", .content = "Why is the sky blue?"},
 * };
 * ollama_chat_request_t req = {.model = "gemma3", .messages = messages, .messages_count = 2};
 * @endcode
 */
typedef struct ollama_message {
    /**
     * Author of the message: "system", "user", "assistant" or "tool". Required in requests
     * (#OLLAMA_ERROR_REQUEST if NULL or empty); in responses NULL if the server omits it.
     */
    const char *role;
    /** Text of the message. In responses never NULL ("" if empty); in a part of a stream, a fragment. */
    const char *content;
    const char *thinking; ///< Reasoning of thinking models (see the `think` request field).
    const ollama_image_t *images; ///< Images attached to the message, in requests only.
    size_t images_count;          ///< Number of `images`; when not 0, `images` must not be NULL.
    const char *tool_name; ///< Name of the tool whose result the message carries, for role "tool".
    /** Tools the model wants to call (responses), or calls made by an assistant message of the history (requests). */
    const ollama_tool_call_t *tool_calls;
    size_t tool_calls_count; ///< Number of `tool_calls`; when not 0, `tool_calls` must not be NULL.
} ollama_message_t;

/** @brief A candidate token and its log probability (Python: `TokenLogprob`). */
typedef struct ollama_token_logprob {
    const char *token; ///< Text of the token.
    double logprob;    ///< Log probability of the token.
} ollama_token_logprob_t;

/**
 * @brief Log probability of a generated token, with the most likely alternatives (Python: `Logprob`).
 *
 * Returned when the request sets `logprobs`, with `top_logprobs` alternatives per token.
 */
typedef struct ollama_logprob {
    const char *token; ///< Text of the generated token.
    double logprob;    ///< Log probability of the token.
    const ollama_token_logprob_t *top_logprobs; ///< The most likely tokens at this position; NULL if none.
    size_t top_logprobs_count;                  ///< Number of `top_logprobs`.
} ollama_logprob_t;

/** @brief Details of a model, as returned by list, show and ps (Python: `ModelDetails`). */
typedef struct ollama_model_details {
    const char *parent_model; ///< Model this one was created from, if any.
    const char *format;       ///< File format, such as "gguf".
    const char *family;       ///< Model family, such as "llama".
    const char *const *families; ///< All the families of the model (non-string items are skipped).
    size_t families_count;       ///< Number of `families`.
    const char *parameter_size;     ///< Number of parameters as text, such as "8.0B".
    const char *quantization_level; ///< Quantization, such as "Q4_K_M".
} ollama_model_details_t;

/** @} */

/**
 * @defgroup requests Requests
 * @brief Parameters of the API calls, one struct per method of the Python client.
 *
 * Fields left at zero / NULL are not sent, so designated initializers only need to mention what
 * the Python call would pass as keyword arguments (the exceptions are noted on the fields):
 *
 * @code{.c}
 * ollama_generate_request_t req = { .model = "llama3.2", .prompt = "Hi" };
 * @endcode
 *
 * Requests only borrow the memory they point to, and only during the call: the request is
 * serialized before anything is sent, so a request passed to a `*_stream` function can be released
 * as soon as that function returns.
 *
 * Common fields:
 * - `model` is required: NULL or "" fails with #OLLAMA_ERROR_REQUEST ("must provide a model").
 * - `options`: JSON object of model options, such as `{"temperature": 0}`, sent as it is.
 * - `format`: the JSON string "json", or a JSON schema object, for structured outputs.
 * - `keep_alive`: how long the model stays loaded after the call. A string that is a JSON number,
 *   such as "300", "0" or "-1", is sent as a number of seconds; any other string, such as "5m",
 *   as a duration (Python: `keep_alive=300` or `keep_alive='5m'`).
 * - `think`: "true" and "false" (case-insensitive) are sent as booleans, any other string, such as the
 *   levels "low", "medium" and "high", as it is (Python: `think=True` or `think='high'`).
 * - Arrays come with a `_count` field; a count other than 0 with a NULL array fails with
 *   #OLLAMA_ERROR_REQUEST.
 * @{
 */

/** @brief Parameters of ollama_generate() and ollama_generate_stream() (Python: `generate()`). */
typedef struct ollama_generate_request {
    const char *model;     ///< Model name, such as "llama3.2"; required.
    const char *prompt;    ///< Prompt to complete.
    const char *suffix;    ///< Text that follows the answer, for fill-in-the-middle models.
    const char *system;    ///< System prompt, overriding the one of the model.
    const char *template_; ///< Prompt template, overriding the one of the model (Python: `template`).
    /** Context returned by a previous call (ollama_generate_response_t::context), to continue from it. */
    const int *context;
    size_t context_count; ///< Number of tokens in `context`; 0 sends no context.
    const char *think;    ///< Thinking for models that support it: "true", "false" or a level.
    bool raw;             ///< true sends the prompt as it is, without applying the template.
    bool logprobs;        ///< true returns the log probabilities of the generated tokens.
    int top_logprobs;     ///< Number of alternatives returned per token with `logprobs`; not sent if <= 0.
    const ollama_json_t *format;  ///< Output format: the JSON string "json" or a JSON schema object.
    const ollama_image_t *images; ///< Images for multimodal models.
    size_t images_count;          ///< Number of `images`.
    const ollama_json_t *options; ///< Model options, a JSON object.
    const char *keep_alive;       ///< How long the model stays loaded: "5m", "300", "-1", ...
    int width; ///< Width of the image, for image generation models (experimental); not sent if <= 0.
    int height; ///< Height of the image, for image generation models (experimental); not sent if <= 0.
    int steps; ///< Number of steps, for image generation models (experimental); not sent if <= 0.
} ollama_generate_request_t;

/** @brief Parameters of ollama_chat() and ollama_chat_stream() (Python: `chat()`). */
typedef struct ollama_chat_request {
    const char *model; ///< Model name, such as "llama3.2"; required.
    /**
     * The conversation so far, oldest first. Always sent: an empty list when `messages_count` is 0.
     * An #ollama_messages_t list can be passed as its `items` and `count`.
     */
    const ollama_message_t *messages;
    size_t messages_count; ///< Number of `messages`.
    /**
     * Tools the model may call: a JSON array of tool definitions, or a single tool, such as built by
     * ollama_tool_new() (Python: `tools=[...]`).
     */
    const ollama_json_t *tools;
    const char *think; ///< Thinking for models that support it: "true", "false" or a level.
    bool logprobs;     ///< true returns the log probabilities of the generated tokens.
    int top_logprobs;  ///< Number of alternatives returned per token with `logprobs`; not sent if <= 0.
    const ollama_json_t *format;  ///< Output format: the JSON string "json" or a JSON schema object.
    const ollama_json_t *options; ///< Model options, a JSON object.
    const char *keep_alive;       ///< How long the model stays loaded: "5m", "300", "-1", ...
} ollama_chat_request_t;

/** @brief A boolean that can also be left to the server, for Python parameters that default to None. */
typedef enum ollama_optional_bool {
    OLLAMA_DEFAULT = 0, ///< Not sent: the server decides. The value of zero-initialized fields.
    OLLAMA_FALSE,       ///< Sent as false.
    OLLAMA_TRUE         ///< Sent as true.
} ollama_optional_bool_t;

/** @brief Parameters of ollama_embed() (Python: `embed()`). */
typedef struct ollama_embed_request {
    const char *model; ///< Embedding model, such as "embeddinggemma"; required.
    /** A single text to embed, used when `inputs_count` is 0. NULL sends an empty string. */
    const char *input;
    /** Several texts to embed, used when `inputs_count` is not 0; NULL items are sent as empty strings. */
    const char *const *inputs;
    size_t inputs_count; ///< Number of `inputs`.
    /** Whether inputs longer than the context are truncated rather than rejected by the server. */
    ollama_optional_bool_t truncate;
    const ollama_json_t *options; ///< Model options, a JSON object.
    const char *keep_alive;       ///< How long the model stays loaded: "5m", "300", "-1", ...
    int dimensions; ///< Number of dimensions of the embeddings, for models that support it; not sent if <= 0.
} ollama_embed_request_t;

/**
 * @brief Parameters of ollama_embeddings() (Python: `embeddings()`).
 *
 * @par Deprecated
 * Use #ollama_embed_request_t and ollama_embed(), which embed several inputs at once.
 */
typedef struct ollama_embeddings_request {
    const char *model;            ///< Embedding model; required.
    const char *prompt;           ///< Text to embed.
    const ollama_json_t *options; ///< Model options, a JSON object.
    const char *keep_alive;       ///< How long the model stays loaded: "5m", "300", "-1", ...
} ollama_embeddings_request_t;

/** @brief Parameters of ollama_create() and ollama_create_stream() (Python: `create()`). */
typedef struct ollama_create_request {
    const char *model;    ///< Name of the model to create; required.
    const char *quantize; ///< Quantization to apply, such as "q4_K_M".
    const char *from_;    ///< Existing model to start from (Python: `from_`).
    /** Model files: JSON object mapping a file name to the digest returned by ollama_create_blob(). */
    const ollama_json_t *files;
    const ollama_json_t *adapters; ///< LoRA adapters: JSON object mapping a file name to a blob digest.
    const char *template_;         ///< Prompt template (Python: `template`).
    const char *license;           ///< License text.
    const char *system;            ///< System prompt.
    const ollama_json_t *parameters; ///< Model parameters, a JSON object like `options`.
    /** Messages stored in the model, such as an example conversation; sent only when `messages_count` is not 0. */
    const ollama_message_t *messages;
    size_t messages_count; ///< Number of `messages`.
} ollama_create_request_t;

/** @brief Parameters of ollama_systemone() (Python: `systemone()`). */
typedef struct ollama_systemone_request {
    const char *model; ///< System One model; required.
    /**
     * What the questions are about: text (a JSON string), or a JSON object or array that System One
     * renders as text; required (#OLLAMA_ERROR_REQUEST if NULL).
     */
    const ollama_json_t *state;
    /**
     * JSON object mapping a question name to a question: an object with a "type" ("noul", "choice"
     * or "score"), "instructions" and "criteria"; required (#OLLAMA_ERROR_REQUEST if it is not an object).
     */
    const ollama_json_t *questions;
    const char *keep_alive; ///< How long the model stays loaded: "5m", "300", "-1", ...
} ollama_systemone_request_t;

/** @} */

/**
 * @defgroup responses Responses
 * @brief Results of the API calls, one struct per response model of the Python library.
 *
 * Responses are allocated by the library and belong to the caller, who releases them with
 * ollama_response_free(). All the strings, arrays and JSON values they point to live exactly as
 * long as the response: copy them (ollama_json_copy(), ollama_messages_add_message(), ...) to
 * keep them longer. The parts of a stream are the exception: they belong to the stream.
 *
 * Fields missing from the answer of the server are NULL (strings, arrays, JSON values), 0 or false,
 * unless noted otherwise. Arrays come with a `_count` field, and the pointer may be NULL when the
 * count is 0. Every response has a `json` field with the whole answer as sent by the server, never
 * NULL, to read fields that this version does not know (Python: `response['field']`).
 *
 * Durations are in nanoseconds, sizes in bytes, and dates are the ISO 8601 strings sent by the
 * server.
 * @{
 */

/**
 * @brief Result of ollama_generate(), or one part of ollama_generate_stream() (Python: `GenerateResponse`).
 *
 * When streaming, each part carries a fragment of the text; the last one has `done` set, the
 * statistics and the context.
 */
typedef struct ollama_generate_response {
    const char *model;       ///< Model that generated the answer.
    const char *created_at;  ///< Creation time (ISO 8601).
    bool done;               ///< true for a complete answer, and for the last part of a stream.
    const char *done_reason; ///< Why the generation stopped, such as "stop" or "length".
    int64_t total_duration;       ///< Total time spent on the request, in nanoseconds.
    int64_t load_duration;        ///< Time spent loading the model, in nanoseconds.
    int64_t prompt_eval_count;    ///< Number of tokens in the prompt.
    int64_t prompt_eval_duration; ///< Time spent evaluating the prompt, in nanoseconds.
    int64_t eval_count;           ///< Number of tokens generated.
    int64_t eval_duration;        ///< Time spent generating, in nanoseconds.
    const char *response; ///< Generated text, never NULL ("" if empty); a fragment when streaming.
    const char *thinking; ///< Reasoning of thinking models; a fragment when streaming.
    /** Encoding of the conversation, to pass as ollama_generate_request_t::context to continue it. */
    const int *context;
    size_t context_count; ///< Number of tokens in `context`.
    const ollama_logprob_t *logprobs; ///< Log probabilities of the generated tokens, when requested.
    size_t logprobs_count;            ///< Number of `logprobs`.
    const char *image; ///< Generated image in base64, for image generation models (see ollama_base64_decode()).
    int64_t completed; ///< Image generation progress when streaming: amount completed so far, out of `total`.
    int64_t total;     ///< Image generation progress when streaming: total amount.
    const ollama_json_t *json; ///< The whole answer (Python: `response['...']`).
} ollama_generate_response_t;

/**
 * @brief Result of ollama_chat(), or one part of ollama_chat_stream() (Python: `ChatResponse`).
 *
 * When streaming, the message of each part carries a fragment of the answer; the last part has
 * `done` set and the statistics. ollama_stream_message() returns the whole message.
 */
typedef struct ollama_chat_response {
    const char *model;       ///< Model that generated the answer.
    const char *created_at;  ///< Creation time (ISO 8601).
    bool done;               ///< true for a complete answer, and for the last part of a stream.
    const char *done_reason; ///< Why the generation stopped, such as "stop" or "length".
    int64_t total_duration;       ///< Total time spent on the request, in nanoseconds.
    int64_t load_duration;        ///< Time spent loading the model, in nanoseconds.
    int64_t prompt_eval_count;    ///< Number of tokens in the prompt.
    int64_t prompt_eval_duration; ///< Time spent evaluating the prompt, in nanoseconds.
    int64_t eval_count;           ///< Number of tokens generated.
    int64_t eval_duration;        ///< Time spent generating, in nanoseconds.
    /**
     * The answer: `content` (never NULL), `thinking` and `tool_calls`. Its strings belong to the
     * response; add it to a history with ollama_messages_add_message().
     */
    ollama_message_t message;
    const ollama_logprob_t *logprobs; ///< Log probabilities of the generated tokens, when requested.
    size_t logprobs_count;            ///< Number of `logprobs`.
    const ollama_json_t *json;        ///< The whole answer (Python: `response['...']`).
} ollama_chat_response_t;

/** @brief One embedding vector. */
typedef struct ollama_embedding {
    const double *values; ///< The components of the vector (non-number items read as 0).
    size_t size;          ///< Number of components, the dimension of the embedding.
} ollama_embedding_t;

/**
 * @brief Result of ollama_embed() (Python: `EmbedResponse`).
 *
 * Its first fields are the ones of #ollama_generate_response_t; those the server does not send for
 * embeddings are NULL or 0.
 */
typedef struct ollama_embed_response {
    const char *model;       ///< Model that computed the embeddings.
    const char *created_at;  ///< Creation time (ISO 8601).
    bool done;               ///< Whether the request is complete.
    const char *done_reason; ///< Why the request ended.
    int64_t total_duration;       ///< Total time spent on the request, in nanoseconds.
    int64_t load_duration;        ///< Time spent loading the model, in nanoseconds.
    int64_t prompt_eval_count;    ///< Number of tokens in the inputs.
    int64_t prompt_eval_duration; ///< Time spent evaluating the inputs, in nanoseconds.
    int64_t eval_count;           ///< Number of tokens generated.
    int64_t eval_duration;        ///< Time spent generating, in nanoseconds.
    const ollama_embedding_t *embeddings; ///< One embedding per input, in the order of the inputs.
    size_t embeddings_count;   ///< Number of `embeddings`.
    const ollama_json_t *json; ///< The whole answer (Python: `response['...']`).
} ollama_embed_response_t;

/**
 * @brief Result of ollama_embeddings() (Python: `EmbeddingsResponse`).
 *
 * @par Deprecated
 * Use ollama_embed() and #ollama_embed_response_t.
 */
typedef struct ollama_embeddings_response {
    const double *embedding;   ///< The components of the embedding (non-number items read as 0).
    size_t embedding_size;     ///< Number of components.
    const ollama_json_t *json; ///< The whole answer (Python: `response['...']`).
} ollama_embeddings_response_t;

/**
 * @brief Status of pull, push and create (Python: `ProgressResponse`).
 *
 * The blocking calls return the final status (`status` is "success"); streams return one per step.
 */
typedef struct ollama_progress_response {
    const char *status; ///< Current step, such as "pulling manifest", or "success" at the end.
    int64_t completed;  ///< Bytes of the current layer transferred so far.
    int64_t total;      ///< Size of the current layer, in bytes.
    const char *digest; ///< Digest of the layer being transferred, such as "sha256:...".
    const ollama_json_t *json; ///< The whole answer (Python: `response['...']`).
} ollama_progress_response_t;

/** @brief A local model, as listed by ollama_list() (Python: `ListResponse.Model`). */
typedef struct ollama_list_model {
    /** Model name, such as "gemma3:latest"; taken from "name" when the server sends no "model". */
    const char *model;
    const char *modified_at; ///< Time of the last change (ISO 8601).
    const char *digest;      ///< Digest of the model.
    int64_t size;            ///< Size of the model on disk, in bytes.
    ollama_model_details_t details; ///< Format, family, parameter size and quantization.
} ollama_list_model_t;

/** @brief Result of ollama_list() (Python: `ListResponse`). */
typedef struct ollama_list_response {
    const ollama_list_model_t *models; ///< The local models.
    size_t models_count;               ///< Number of `models`.
    const ollama_json_t *json;         ///< The whole answer (Python: `response['...']`).
} ollama_list_response_t;

/** @brief Result of ollama_show() (Python: `ShowResponse`). */
typedef struct ollama_show_response {
    const char *modified_at; ///< Time of the last change (ISO 8601).
    const char *template_;   ///< Prompt template (Python: `template`).
    const char *modelfile;   ///< Modelfile of the model.
    const char *license;     ///< License text.
    ollama_model_details_t details; ///< Format, family, parameter size and quantization.
    /** Model metadata, the "model_info" object, such as "general.architecture" (Python: `modelinfo`). */
    const ollama_json_t *modelinfo;
    const char *parameters; ///< Model parameters as text, one per line.
    /** What the model can do, such as "completion", "tools", "vision" or "thinking" (non-string items are skipped). */
    const char *const *capabilities;
    size_t capabilities_count; ///< Number of `capabilities`.
    const ollama_json_t *json; ///< The whole answer (Python: `response['...']`).
} ollama_show_response_t;

/** @brief A model loaded in memory, as listed by ollama_ps() (Python: `ProcessResponse.Model`). */
typedef struct ollama_process_model {
    /** The "model" field, such as "gemma3:latest"; NULL if the server omits it (no fallback to `name`). */
    const char *model;
    const char *name;       ///< The "name" field, usually the same as `model`.
    const char *digest;     ///< Digest of the model.
    const char *expires_at; ///< When the model will be unloaded (ISO 8601).
    int64_t size;           ///< Memory used by the model, in bytes.
    int64_t size_vram;      ///< Part of `size` in GPU memory, in bytes.
    ollama_model_details_t details; ///< Format, family, parameter size and quantization.
    int64_t context_length; ///< Context length the model was loaded with, in tokens.
} ollama_process_model_t;

/** @brief Result of ollama_ps() (Python: `ProcessResponse`). */
typedef struct ollama_process_response {
    const ollama_process_model_t *models; ///< The models loaded in memory.
    size_t models_count;                  ///< Number of `models`.
    const ollama_json_t *json;            ///< The whole answer (Python: `response['...']`).
} ollama_process_response_t;

/** @brief One result of ollama_web_search() (Python: `WebSearchResult`). */
typedef struct ollama_web_search_result {
    const char *content; ///< Relevant content of the page.
    const char *title;   ///< Title of the page.
    const char *url;     ///< URL of the page.
} ollama_web_search_result_t;

/** @brief Result of ollama_web_search() (Python: `WebSearchResponse`). */
typedef struct ollama_web_search_response {
    const ollama_web_search_result_t *results; ///< The search results.
    size_t results_count;                      ///< Number of `results`.
    const ollama_json_t *json;                 ///< The whole answer (Python: `response['...']`).
} ollama_web_search_response_t;

/** @brief Result of ollama_web_fetch() (Python: `WebFetchResponse`). */
typedef struct ollama_web_fetch_response {
    const char *title;        ///< Title of the page.
    const char *content;      ///< Content of the page.
    const char *const *links; ///< Links found on the page (non-string items are skipped).
    size_t links_count;       ///< Number of `links`.
    const ollama_json_t *json; ///< The whole answer (Python: `response['...']`).
} ollama_web_fetch_response_t;

/**
 * @brief The answer to one question of ollama_systemone() (Python: `SystemOneNoulAnswer`,
 * `SystemOneChoiceAnswer` and `SystemOneScoreAnswer`).
 *
 * The fields that are set depend on `type`; the others are NULL or 0.
 */
typedef struct ollama_systemone_answer {
    const char *name;   ///< Name of the question, as in the request; never NULL.
    const char *type;   ///< Type of the question: "noul", "choice" or "score".
    double noul;        ///< noul: probability of true.
    const char *choice; ///< choice: the selected choice.
    double score;       ///< score: expected zero-based score, which may be fractional.
    double confidence;  ///< choice and score: confidence of the answer.
    /** choice and score: JSON object mapping each choice (or score) to its probability. */
    const ollama_json_t *probabilities;
    const ollama_json_t *legend;        ///< score: JSON object mapping each score to its description.
} ollama_systemone_answer_t;

/** @brief Result of ollama_systemone() (Python: `SystemOneResponse`). */
typedef struct ollama_systemone_response {
    const char *model; ///< Model that answered.
    const ollama_systemone_answer_t *answers; ///< One answer per question, in the order sent by the server.
    size_t answers_count;                     ///< Number of `answers`.
    /** Tokens used by the request (Python: `SystemOneUsage`). */
    struct {
        int64_t input_tokens;  ///< Number of tokens read.
        int64_t output_tokens; ///< Number of tokens generated.
    } usage;
    const ollama_json_t *json; ///< The whole answer (Python: `response['...']`).
} ollama_systemone_response_t;

/**
 * @brief Releases a response returned by one of the API functions, with everything it points to.
 *
 * It works for every response type. Do not call it on the parts returned by the
 * `ollama_stream_next_*()` functions: they belong to their stream.
 *
 * @param[in] response the response; NULL is ignored
 */
OLLAMA_API void ollama_response_free(void *response);

/** @} */

/**
 * @defgroup api API calls
 * @brief One blocking function per method of the Python client.
 *
 * All the functions follow the same rules:
 * - `client` is the client to use, or NULL for the default client (see @ref client).
 * - Requests are only read during the call.
 * - The result is stored in `*response` (`*digest` for ollama_create_blob()), which is set to NULL
 *   first and stays NULL on failure. On success it belongs to the caller: release it with
 *   ollama_response_free() (ollama_free() for the digest).
 * - The function returns #OLLAMA_OK or an error code, and the details of the error are available
 *   from ollama_last_error(client) until the next call on the client.
 *
 * @anchor common_errors
 * Besides the checks of the arguments described for each function, every call can fail with:
 * - #OLLAMA_ERROR_CONNECTION: the server cannot be reached (connection refused, unknown host or
 *   proxy). The message is the one of ollama-python, "Failed to connect to Ollama. Please check
 *   that Ollama is downloaded, running and accessible. https://ollama.com/download".
 * - #OLLAMA_ERROR_TRANSPORT: any other network failure, such as a timeout (see
 *   ollama_client_set_timeout()), a TLS error, an interrupted transfer or too many redirects; the
 *   message comes from libcurl.
 * - #OLLAMA_ERROR_RESPONSE: the server answered with an HTTP status outside 200-299, which is
 *   stored in ollama_error_t::status_code; the message is the "error" field of the JSON answer
 *   (see ollama_error_t::message).
 * - #OLLAMA_ERROR_PARSE: the answer is not valid JSON (only for the calls that return a response).
 * - #OLLAMA_ERROR_MEMORY: out of memory.
 * - With a NULL client, when the default client cannot be created: #OLLAMA_ERROR_REQUEST if
 *   OLLAMA_HOST or OLLAMA_API_KEY is invalid, #OLLAMA_ERROR_MEMORY otherwise. Nothing is sent and
 *   ollama_last_error(NULL) gives the reason.
 *
 * Redirects are followed, as with `follow_redirects=True` in the Python client.
 * @{
 */

/**
 * @brief Generates a response for a prompt (`POST /api/generate`; Python: `generate()`).
 *
 * It waits for the whole answer; ollama_generate_stream() returns it as it is generated.
 *
 * @code{.c}
 * ollama_generate_request_t req = {.model = "gemma3", .prompt = "Why is the sky blue?"};
 * ollama_generate_response_t *res;
 * if (ollama_generate(NULL, &req, &res) == OLLAMA_OK) {
 *     puts(res->response);
 *     ollama_response_free(res);
 * } else {
 *     fprintf(stderr, "error: %s\n", ollama_last_error(NULL)->message);
 * }
 * @endcode
 *
 * @param[in] client the client, or NULL for the default client
 * @param[in] request the parameters
 * @param[out] response receives the response, to release with ollama_response_free(); NULL on failure
 * @return #OLLAMA_OK on success. Otherwise #OLLAMA_ERROR_REQUEST if @p request or @p response is
 *         NULL, the model is missing, a `_count` is set with a NULL array or an image has neither
 *         path, data nor base64; #OLLAMA_ERROR_IO if an image file cannot be read; or one of the
 *         @ref common_errors "errors common to all calls".
 */
OLLAMA_API ollama_status_t ollama_generate(ollama_client_t *client, const ollama_generate_request_t *request,
                                           ollama_generate_response_t **response);

/**
 * @brief Generates the next message of a chat (`POST /api/chat`; Python: `chat()`).
 *
 * It waits for the whole answer; ollama_chat_stream() returns it as it is generated. See
 * #ollama_messages_t to keep a history and ollama_tool_new() for tool calling.
 *
 * @code{.c}
 * ollama_message_t messages[] = {{.role = "user", .content = "Why is the sky blue?"}};
 * ollama_chat_request_t req = {.model = "gemma3", .messages = messages, .messages_count = 1};
 * ollama_chat_response_t *res;
 * if (ollama_chat(client, &req, &res) == OLLAMA_OK) {
 *     puts(res->message.content);
 *     ollama_response_free(res);
 * }
 * @endcode
 *
 * @param[in] client the client, or NULL for the default client
 * @param[in] request the parameters
 * @param[out] response receives the response, to release with ollama_response_free(); NULL on failure
 * @return #OLLAMA_OK on success. Otherwise #OLLAMA_ERROR_REQUEST if @p request or @p response is
 *         NULL, the model is missing, a message has no role, a `_count` is set with a NULL array or
 *         an image has neither path, data nor base64; #OLLAMA_ERROR_IO if an image file cannot be
 *         read; or one of the @ref common_errors "errors common to all calls".
 */
OLLAMA_API ollama_status_t ollama_chat(ollama_client_t *client, const ollama_chat_request_t *request,
                                       ollama_chat_response_t **response);

/**
 * @brief Computes the embeddings of one or more inputs (`POST /api/embed`; Python: `embed()`).
 *
 * @code{.c}
 * const char *inputs[] = {"first text", "second text"};
 * ollama_embed_request_t req = {.model = "embeddinggemma", .inputs = inputs, .inputs_count = 2};
 * ollama_embed_response_t *res;
 * if (ollama_embed(client, &req, &res) == OLLAMA_OK) {
 *     for (size_t i = 0; i < res->embeddings_count; i++)
 *         printf("input %zu: %zu dimensions\n", i, res->embeddings[i].size);
 *     ollama_response_free(res);
 * }
 * @endcode
 *
 * @param[in] client the client, or NULL for the default client
 * @param[in] request the parameters
 * @param[out] response receives the response, to release with ollama_response_free(); NULL on failure
 * @return #OLLAMA_OK on success. Otherwise #OLLAMA_ERROR_REQUEST if @p request or @p response is
 *         NULL, the model is missing or `inputs_count` is set with NULL `inputs`; or one of the
 *         @ref common_errors "errors common to all calls".
 */
OLLAMA_API ollama_status_t ollama_embed(ollama_client_t *client, const ollama_embed_request_t *request,
                                        ollama_embed_response_t **response);

/**
 * @brief Computes the embedding of a prompt (`POST /api/embeddings`; Python: `embeddings()`).
 *
 * @par Deprecated
 * Use ollama_embed(), like the Python library.
 *
 * @param[in] client the client, or NULL for the default client
 * @param[in] request the parameters
 * @param[out] response receives the response, to release with ollama_response_free(); NULL on failure
 * @return #OLLAMA_OK on success. Otherwise #OLLAMA_ERROR_REQUEST if @p request or @p response is
 *         NULL or the model is missing, or one of the @ref common_errors "errors common to all calls".
 */
OLLAMA_API ollama_status_t ollama_embeddings(ollama_client_t *client, const ollama_embeddings_request_t *request,
                                             ollama_embeddings_response_t **response);

/**
 * @brief Downloads a model and waits for the end of the download (`POST /api/pull`; Python: `pull()`).
 *
 * The server sends nothing until the download is complete: use ollama_pull_stream() to report
 * the progress, or when the client has a timeout.
 *
 * @param[in] client the client, or NULL for the default client
 * @param[in] model name of the model, such as "gemma3"
 * @param[in] insecure true to allow insecure connections to the registry (Python: `insecure=True`)
 * @param[out] response receives the final status, to release with ollama_response_free(); NULL on
 *             failure
 * @return #OLLAMA_OK on success. Otherwise #OLLAMA_ERROR_REQUEST if @p response is NULL or
 *         @p model is NULL or "", or one of the @ref common_errors "errors common to all calls".
 */
OLLAMA_API ollama_status_t ollama_pull(ollama_client_t *client, const char *model, bool insecure,
                                       ollama_progress_response_t **response);
/**
 * @brief Uploads a model to a registry and waits for the end of the upload (`POST /api/push`;
 * Python: `push()`).
 *
 * The server sends nothing until the upload is complete: use ollama_push_stream() to report the
 * progress, or when the client has a timeout.
 *
 * @param[in] client the client, or NULL for the default client
 * @param[in] model name of the model, including its namespace, such as "user/model:tag"
 * @param[in] insecure true to allow insecure connections to the registry (Python: `insecure=True`)
 * @param[out] response receives the final status, to release with ollama_response_free(); NULL on
 *             failure
 * @return #OLLAMA_OK on success. Otherwise #OLLAMA_ERROR_REQUEST if @p response is NULL or
 *         @p model is NULL or "", or one of the @ref common_errors "errors common to all calls".
 */
OLLAMA_API ollama_status_t ollama_push(ollama_client_t *client, const char *model, bool insecure,
                                       ollama_progress_response_t **response);

/**
 * @brief Creates a model and waits for the end of the creation (`POST /api/create`; Python: `create()`).
 *
 * The server sends nothing until the model is created: use ollama_create_stream() to report the
 * progress, or when the client has a timeout.
 *
 * @code{.c}
 * ollama_create_request_t req = {.model = "mario", .from_ = "gemma3", .system = "You are Mario."};
 * ollama_progress_response_t *res;
 * if (ollama_create(client, &req, &res) == OLLAMA_OK) {
 *     puts(res->status); // "success"
 *     ollama_response_free(res);
 * }
 * @endcode
 *
 * @param[in] client the client, or NULL for the default client
 * @param[in] request the parameters
 * @param[out] response receives the final status, to release with ollama_response_free(); NULL on
 *             failure
 * @return #OLLAMA_OK on success. Otherwise #OLLAMA_ERROR_REQUEST if @p request or @p response is
 *         NULL, the model is missing or `messages` is invalid (see ollama_chat());
 *         #OLLAMA_ERROR_IO if an image file of `messages` cannot be read; or one of the
 *         @ref common_errors "errors common to all calls".
 */
OLLAMA_API ollama_status_t ollama_create(ollama_client_t *client, const ollama_create_request_t *request,
                                         ollama_progress_response_t **response);

/**
 * @brief Uploads a file as a blob (`POST /api/blobs/sha256:...`; Python: `create_blob()`).
 *
 * The file is read twice: once to compute its SHA-256 digest, then to upload it, so it must not
 * change during the call. The digest identifies the blob in the `files` and `adapters` of
 * #ollama_create_request_t.
 *
 * @code{.c}
 * char *digest;
 * if (ollama_create_blob(client, "model.gguf", &digest) == OLLAMA_OK) {
 *     ollama_json_t *files = ollama_json_object();
 *     ollama_json_set_string(files, "model.gguf", digest);
 *     ollama_free(digest);
 *     ollama_create_request_t req = {.model = "my-model", .files = files};
 *     // ... ollama_create(client, &req, &res) ...
 *     ollama_json_free(files);
 * }
 * @endcode
 *
 * @param[in] client the client, or NULL for the default client
 * @param[in] path path of the file to upload, passed to fopen() (on Windows, in the active code page)
 * @param[out] digest receives the digest of the file, "sha256:" followed by 64 lowercase
 *             hexadecimal digits, to release with ollama_free(); NULL on failure
 * @return #OLLAMA_OK on success. Otherwise #OLLAMA_ERROR_REQUEST if @p path or @p digest is NULL;
 *         #OLLAMA_ERROR_IO if the file cannot be opened, or read while its digest is computed (a
 *         read error during the upload gives #OLLAMA_ERROR_TRANSPORT); or one of the
 *         @ref common_errors "errors common to all calls" except #OLLAMA_ERROR_PARSE (the answer is
 *         not read).
 */
OLLAMA_API ollama_status_t ollama_create_blob(ollama_client_t *client, const char *path, char **digest);

/**
 * @brief Lists the local models (`GET /api/tags`; Python: `list()`).
 *
 * @param[in] client the client, or NULL for the default client
 * @param[out] response receives the list, to release with ollama_response_free(); NULL on failure
 * @return #OLLAMA_OK on success. Otherwise #OLLAMA_ERROR_REQUEST if @p response is NULL, or one of
 *         the @ref common_errors "errors common to all calls".
 */
OLLAMA_API ollama_status_t ollama_list(ollama_client_t *client, ollama_list_response_t **response);

/**
 * @brief Deletes a model (`DELETE /api/delete`; Python: `delete()`).
 *
 * @param[in] client the client, or NULL for the default client
 * @param[in] model name of the model to delete
 * @return #OLLAMA_OK on success. Otherwise #OLLAMA_ERROR_REQUEST if @p model is NULL or "", or one
 *         of the @ref common_errors "errors common to all calls" except #OLLAMA_ERROR_PARSE (the
 *         answer is not read); a missing model gives #OLLAMA_ERROR_RESPONSE with status code 404.
 */
OLLAMA_API ollama_status_t ollama_delete(ollama_client_t *client, const char *model);

/**
 * @brief Copies a model under a new name (`POST /api/copy`; Python: `copy()`).
 *
 * @param[in] client the client, or NULL for the default client
 * @param[in] source name of the existing model
 * @param[in] destination name of the copy
 * @return #OLLAMA_OK on success. Otherwise #OLLAMA_ERROR_REQUEST if @p source or @p destination is
 *         NULL, or one of the @ref common_errors "errors common to all calls" except
 *         #OLLAMA_ERROR_PARSE (the answer is not read).
 */
OLLAMA_API ollama_status_t ollama_copy(ollama_client_t *client, const char *source, const char *destination);

/**
 * @brief Shows information about a model (`POST /api/show`; Python: `show()`).
 *
 * @param[in] client the client, or NULL for the default client
 * @param[in] model name of the model
 * @param[out] response receives the information, to release with ollama_response_free(); NULL on
 *             failure
 * @return #OLLAMA_OK on success. Otherwise #OLLAMA_ERROR_REQUEST if @p response is NULL or
 *         @p model is NULL or "", or one of the @ref common_errors "errors common to all calls".
 */
OLLAMA_API ollama_status_t ollama_show(ollama_client_t *client, const char *model, ollama_show_response_t **response);

/**
 * @brief Lists the models loaded in memory (`GET /api/ps`; Python: `ps()`).
 *
 * @param[in] client the client, or NULL for the default client
 * @param[out] response receives the list, to release with ollama_response_free(); NULL on failure
 * @return #OLLAMA_OK on success. Otherwise #OLLAMA_ERROR_REQUEST if @p response is NULL, or one of
 *         the @ref common_errors "errors common to all calls".
 */
OLLAMA_API ollama_status_t ollama_ps(ollama_client_t *client, ollama_process_response_t **response);

/**
 * @brief Searches the web through ollama.com (Python: `web_search()`).
 *
 * The request goes to https://ollama.com/api/web_search whatever the host of the client, and
 * needs an API key: the client must have an "Authorization" header starting with "Bearer ", which
 * ollama_client_new() adds when OLLAMA_API_KEY is set.
 *
 * @param[in] client the client, or NULL for the default client
 * @param[in] query the search query
 * @param[in] max_results maximum number of results; 0 or a negative value uses the default of 3
 * @param[out] response receives the results, to release with ollama_response_free(); NULL on failure
 * @return #OLLAMA_OK on success. Otherwise #OLLAMA_ERROR_REQUEST if @p response or @p query is NULL
 *         or the client has no Bearer token (nothing is sent then), or one of the
 *         @ref common_errors "errors common to all calls".
 */
OLLAMA_API ollama_status_t ollama_web_search(ollama_client_t *client, const char *query, int max_results,
                                             ollama_web_search_response_t **response);
/**
 * @brief Fetches a web page through ollama.com (Python: `web_fetch()`).
 *
 * The request goes to https://ollama.com/api/web_fetch whatever the host of the client, and
 * needs an API key, like ollama_web_search().
 *
 * @param[in] client the client, or NULL for the default client
 * @param[in] url the URL of the page
 * @param[out] response receives the page, to release with ollama_response_free(); NULL on failure
 * @return #OLLAMA_OK on success. Otherwise #OLLAMA_ERROR_REQUEST if @p response or @p url is NULL
 *         or the client has no Bearer token (nothing is sent then), or one of the
 *         @ref common_errors "errors common to all calls".
 */
OLLAMA_API ollama_status_t ollama_web_fetch(ollama_client_t *client, const char *url,
                                            ollama_web_fetch_response_t **response);

/**
 * @brief Scores questions with a local System One model (`POST /v1/systemone`; Python: `systemone()`).
 *
 * There is no streaming variant.
 *
 * @code{.c}
 * ollama_json_t *state = ollama_json_string("Why is the sky blue?");
 * ollama_json_t *questions = ollama_json_parse(
 *     "{\"is_question\": {\"type\": \"noul\", \"instructions\": \"Is this a question?\"}}");
 * ollama_systemone_request_t req = {.model = "systemone", .state = state, .questions = questions};
 * ollama_systemone_response_t *res;
 * if (ollama_systemone(client, &req, &res) == OLLAMA_OK) {
 *     for (size_t i = 0; i < res->answers_count; i++)
 *         printf("%s: %.2f\n", res->answers[i].name, res->answers[i].noul);
 *     ollama_response_free(res);
 * }
 * ollama_json_free(state);
 * ollama_json_free(questions);
 * @endcode
 *
 * @param[in] client the client, or NULL for the default client
 * @param[in] request the parameters
 * @param[out] response receives the answers, to release with ollama_response_free(); NULL on failure
 * @return #OLLAMA_OK on success. Otherwise #OLLAMA_ERROR_REQUEST if @p request or @p response is
 *         NULL, the model is missing, `state` is NULL or `questions` is not a JSON object; or one of
 *         the @ref common_errors "errors common to all calls".
 */
OLLAMA_API ollama_status_t ollama_systemone(ollama_client_t *client, const ollama_systemone_request_t *request,
                                            ollama_systemone_response_t **response);

/** @} */

/**
 * @defgroup streaming Streaming
 * @brief Iterators over the parts of an answer (Python: `stream=True`).
 *
 * The `*_stream` functions are the equivalent of `stream=True`. They send the request and wait
 * for the beginning of the answer, so the errors returned by the server before the first part,
 * such as "model not found", are reported by the `*_stream` function itself, through its status
 * code and ollama_last_error(). They return an iterator over the parts of the answer, read with
 * the `ollama_stream_next_*()` function that matches the stream:
 *
 * @code{.c}
 * ollama_stream_t *stream;
 * if (ollama_chat_stream(client, &req, &stream) == OLLAMA_OK) {
 *     const ollama_chat_response_t *part;
 *     while ((part = ollama_stream_next_chat(stream)) != NULL)
 *         fputs(part->message.content, stdout);
 *     if (ollama_stream_error(stream)->code != OLLAMA_OK)
 *         fprintf(stderr, "error: %s\n", ollama_stream_error(stream)->message);
 *     ollama_stream_free(stream);
 * } else {
 *     fprintf(stderr, "error: %s\n", ollama_last_error(client)->message);
 * }
 * @endcode
 *
 * Errors that happen after the first part end the iteration and are reported by
 * ollama_stream_error(). A part belongs to the stream and is valid until the next call on the
 * stream that reads a part, or ollama_stream_free(). Once opened, a stream does not depend on
 * its client, which remains available for other calls.
 * @{
 */

/** @brief Opaque stream, opened by the `*_stream` functions and closed with ollama_stream_free(). */
typedef struct ollama_stream ollama_stream_t;

/**
 * @brief Starts a streamed generation (`POST /api/generate`; Python: `generate(..., stream=True)`).
 *
 * Read the parts with ollama_stream_next_generate(): each one carries a fragment of the text in
 * `response`, and the last one has `done` set and the statistics.
 *
 * @param[in] client the client, or NULL for the default client
 * @param[in] request the parameters; it can be released as soon as this function returns
 * @param[out] stream receives the stream, to close with ollama_stream_free(); NULL on failure
 * @return #OLLAMA_OK on success. Otherwise the errors of ollama_generate(), except
 *         #OLLAMA_ERROR_PARSE: invalid parts are reported by ollama_stream_error().
 */
OLLAMA_API ollama_status_t ollama_generate_stream(ollama_client_t *client, const ollama_generate_request_t *request,
                                                  ollama_stream_t **stream);
/**
 * @brief Starts a streamed chat (`POST /api/chat`; Python: `chat(..., stream=True)`).
 *
 * Read the parts with ollama_stream_next_chat(): the message of each one carries a fragment of
 * the answer. ollama_stream_message() assembles the whole message.
 *
 * @param[in] client the client, or NULL for the default client
 * @param[in] request the parameters; it can be released as soon as this function returns
 * @param[out] stream receives the stream, to close with ollama_stream_free(); NULL on failure
 * @return #OLLAMA_OK on success. Otherwise the errors of ollama_chat(), except
 *         #OLLAMA_ERROR_PARSE: invalid parts are reported by ollama_stream_error().
 */
OLLAMA_API ollama_status_t ollama_chat_stream(ollama_client_t *client, const ollama_chat_request_t *request,
                                              ollama_stream_t **stream);
/**
 * @brief Starts downloading a model, reporting the progress (`POST /api/pull`; Python: `pull(..., stream=True)`).
 *
 * Read the progress with ollama_stream_next_progress(); the last part has the status "success".
 *
 * @code{.c}
 * ollama_stream_t *stream;
 * if (ollama_pull_stream(client, "gemma3", false, &stream) == OLLAMA_OK) {
 *     const ollama_progress_response_t *p;
 *     while ((p = ollama_stream_next_progress(stream)) != NULL)
 *         printf("%s %lld/%lld\n", p->status, (long long)p->completed, (long long)p->total);
 *     ollama_stream_free(stream);
 * }
 * @endcode
 *
 * @param[in] client the client, or NULL for the default client
 * @param[in] model name of the model, such as "gemma3"
 * @param[in] insecure true to allow insecure connections to the registry
 * @param[out] stream receives the stream, to close with ollama_stream_free(); NULL on failure
 * @return #OLLAMA_OK on success. Otherwise the errors of ollama_pull(), except
 *         #OLLAMA_ERROR_PARSE: invalid parts are reported by ollama_stream_error().
 */
OLLAMA_API ollama_status_t ollama_pull_stream(ollama_client_t *client, const char *model, bool insecure,
                                              ollama_stream_t **stream);
/**
 * @brief Starts uploading a model, reporting the progress (`POST /api/push`; Python: `push(..., stream=True)`).
 *
 * Read the progress with ollama_stream_next_progress(); the last part has the status "success".
 *
 * @param[in] client the client, or NULL for the default client
 * @param[in] model name of the model, including its namespace, such as "user/model:tag"
 * @param[in] insecure true to allow insecure connections to the registry
 * @param[out] stream receives the stream, to close with ollama_stream_free(); NULL on failure
 * @return #OLLAMA_OK on success. Otherwise the errors of ollama_push(), except
 *         #OLLAMA_ERROR_PARSE: invalid parts are reported by ollama_stream_error().
 */
OLLAMA_API ollama_status_t ollama_push_stream(ollama_client_t *client, const char *model, bool insecure,
                                              ollama_stream_t **stream);
/**
 * @brief Starts creating a model, reporting the progress (`POST /api/create`; Python: `create(..., stream=True)`).
 *
 * Read the progress with ollama_stream_next_progress(); the last part has the status "success".
 *
 * @param[in] client the client, or NULL for the default client
 * @param[in] request the parameters; it can be released as soon as this function returns
 * @param[out] stream receives the stream, to close with ollama_stream_free(); NULL on failure
 * @return #OLLAMA_OK on success. Otherwise the errors of ollama_create(), except
 *         #OLLAMA_ERROR_PARSE: invalid parts are reported by ollama_stream_error().
 */
OLLAMA_API ollama_status_t ollama_create_stream(ollama_client_t *client, const ollama_create_request_t *request,
                                                ollama_stream_t **stream);

/**
 * @brief Reads the next part of a stream opened with ollama_generate_stream().
 *
 * It blocks until a part is available. The part belongs to the stream: it is valid until the next
 * call that reads a part of the stream, or ollama_stream_free(), and must not be passed to
 * ollama_response_free().
 *
 * @param[in,out] stream the stream; NULL returns NULL
 * @return The part, or NULL at the end of the stream or on error; ollama_stream_error() tells
 *         them apart. The errors are #OLLAMA_ERROR_PARSE for a line that is not valid JSON,
 *         #OLLAMA_ERROR_RESPONSE when the server sends an "error" (Python: ResponseError),
 *         #OLLAMA_ERROR_TRANSPORT or #OLLAMA_ERROR_CONNECTION when the transfer fails,
 *         #OLLAMA_ERROR_MEMORY, and #OLLAMA_ERROR_REQUEST when the stream was not opened by
 *         ollama_generate_stream(). After an error, every call returns NULL.
 */
OLLAMA_API const ollama_generate_response_t *ollama_stream_next_generate(ollama_stream_t *stream);
/**
 * @brief Reads the next part of a stream opened with ollama_chat_stream().
 *
 * It behaves like ollama_stream_next_generate(), and also adds the part to the message returned
 * by ollama_stream_message().
 *
 * @param[in,out] stream the stream; NULL returns NULL
 * @return The part, owned by the stream, or NULL at the end of the stream or on error (see
 *         ollama_stream_next_generate(); #OLLAMA_ERROR_REQUEST when the stream was not opened by
 *         ollama_chat_stream()).
 */
OLLAMA_API const ollama_chat_response_t *ollama_stream_next_chat(ollama_stream_t *stream);
/**
 * @brief Reads the next part of a stream opened with ollama_pull_stream(), ollama_push_stream() or
 * ollama_create_stream().
 *
 * It behaves like ollama_stream_next_generate().
 *
 * @param[in,out] stream the stream; NULL returns NULL
 * @return The part, owned by the stream, or NULL at the end of the stream or on error (see
 *         ollama_stream_next_generate(); #OLLAMA_ERROR_REQUEST when the stream is a generate or
 *         chat stream).
 */
OLLAMA_API const ollama_progress_response_t *ollama_stream_next_progress(ollama_stream_t *stream);

/**
 * @brief Returns the error that ended a stream.
 *
 * Only the errors that happen while the parts are read are reported here; the errors of the
 * `*_stream` function that opened the stream are reported by ollama_last_error(). The status code
 * of these errors is always -1.
 *
 * @param[in] stream the stream, or NULL
 * @return The error, never NULL: its code is #OLLAMA_OK while the stream is running and when it
 *         ended normally. It belongs to the stream and is valid until ollama_stream_free(). For a
 *         NULL stream, a static #OLLAMA_ERROR_REQUEST error ("stream is NULL").
 */
OLLAMA_API const ollama_error_t *ollama_stream_error(const ollama_stream_t *stream);

/**
 * @brief Returns the message assembled from the parts of a chat stream read so far.
 *
 * The role is the first one sent by the server ("assistant" before any), the content and the
 * thinking are the concatenation of the fragments (thinking is NULL while there is none), and the
 * tool calls of all the parts are collected; `tool_name` and `images` are NULL. Handy to append
 * the answer to the history with ollama_messages_add_message(), even when the stream ended early.
 * When it ended with #OLLAMA_ERROR_MEMORY, the message is valid but may miss the last part.
 *
 * @code{.c}
 * while (ollama_stream_next_chat(stream) != NULL) {}
 * ollama_messages_add_message(history, ollama_stream_message(stream));
 * ollama_stream_free(stream);
 * @endcode
 *
 * @param[in] stream the stream
 * @return The message, owned by the stream and valid until ollama_stream_free(). Each call to
 *         ollama_stream_next_chat() updates its fields, which may invalidate the strings and arrays
 *         read before. NULL if @p stream is NULL or not a chat stream.
 */
OLLAMA_API const ollama_message_t *ollama_stream_message(const ollama_stream_t *stream);

/**
 * @brief Closes a stream and releases it, aborting the request if it is still running.
 *
 * The current part and the message of ollama_stream_message() are released too. Closing a stream
 * before its end aborts the HTTP request: this is how to stop a generation early.
 *
 * @param[in] stream the stream; NULL is ignored
 */
OLLAMA_API void ollama_stream_free(ollama_stream_t *stream);

/** @} */

/**
 * @defgroup helpers Helpers
 * @brief Chat histories, tool definitions, base64 and memory release.
 *
 * These functions do not use a client: they report errors only through their return value.
 * @{
 */

/**
 * @brief A growable list of messages that owns deep copies of them, for chat histories.
 *
 * `items` and `count` can be passed as the messages of a request. Read them again after adding a
 * message, since `items` moves when the list grows, and do not modify the fields directly.
 *
 * @code{.c}
 * ollama_messages_t *history = ollama_messages_new();
 * ollama_messages_add(history, "user", "Why is the sky blue?");
 * ollama_chat_request_t req = {.model = "gemma3", .messages = history->items, .messages_count = history->count};
 * ollama_chat_response_t *res;
 * if (ollama_chat(client, &req, &res) == OLLAMA_OK) {
 *     ollama_messages_add_message(history, &res->message); // deep copy: res can be released
 *     ollama_response_free(res);
 * }
 * // ... more turns ...
 * ollama_messages_free(history);
 * @endcode
 */
typedef struct ollama_messages {
    ollama_message_t *items; ///< The messages, oldest first; their memory belongs to the list.
    size_t count;            ///< Number of messages.
    size_t capacity;         ///< Number of allocated `items`.
} ollama_messages_t;

/**
 * @brief Creates an empty list of messages.
 * @return A new list to release with ollama_messages_free(), or NULL if memory runs out.
 */
OLLAMA_API ollama_messages_t *ollama_messages_new(void);
/**
 * @brief Releases a list of messages with its copies of the messages.
 * @param[in] messages the list; NULL is ignored
 */
OLLAMA_API void ollama_messages_free(ollama_messages_t *messages);
/**
 * @brief Appends a message with the given role and content, both copied.
 *
 * @param[in,out] messages the list
 * @param[in] role the role, such as "user"; it is not checked here, but a chat request fails with
 *            a NULL or empty role
 * @param[in] content the text, or NULL
 * @return #OLLAMA_OK or an error code:
 * @retval OLLAMA_OK the message was appended
 * @retval OLLAMA_ERROR_REQUEST @p messages is NULL
 * @retval OLLAMA_ERROR_MEMORY out of memory; the list keeps its messages
 */
OLLAMA_API ollama_status_t ollama_messages_add(ollama_messages_t *messages, const char *role, const char *content);
/**
 * @brief Appends a deep copy of a message, such as the message of a chat response.
 *
 * The strings, the images (including their bytes) and the tool calls with their arguments are
 * copied, so the original can be released afterwards: for example `&res->message` before
 * ollama_response_free(res), or the message of ollama_stream_message() before ollama_stream_free().
 *
 * @param[in,out] messages the list
 * @param[in] message the message to copy
 * @return #OLLAMA_OK or an error code:
 * @retval OLLAMA_OK the message was appended
 * @retval OLLAMA_ERROR_REQUEST @p messages or @p message is NULL
 * @retval OLLAMA_ERROR_MEMORY out of memory; the list keeps its messages
 */
OLLAMA_API ollama_status_t ollama_messages_add_message(ollama_messages_t *messages, const ollama_message_t *message);

/**
 * @brief Builds the definition of a function tool, the C replacement for passing Python functions as tools.
 *
 * The result is the JSON object below; add parameters with ollama_tool_add_parameter().
 * @verbatim
   {"type": "function", "function": {"name": NAME, "description": DESCRIPTION,
    "parameters": {"type": "object", "properties": {}, "required": []}}}
   @endverbatim
 *
 * A single tool can be passed as the `tools` of a chat request; several tools go in a JSON array
 * built with ollama_json_array() and ollama_json_append().
 *
 * @code{.c}
 * ollama_json_t *tool = ollama_tool_new("get_weather", "Get the current weather for a city");
 * ollama_tool_add_parameter(tool, "city", "string", "The name of the city", true);
 * ollama_chat_request_t req = {.model = "llama3.1", .messages = msgs, .messages_count = n, .tools = tool};
 * ollama_chat_response_t *res;
 * if (ollama_chat(client, &req, &res) == OLLAMA_OK) {
 *     for (size_t i = 0; i < res->message.tool_calls_count; i++) {
 *         const ollama_tool_call_t *call = &res->message.tool_calls[i];
 *         const char *city = ollama_json_as_string(ollama_json_get(call->function.arguments, "city"));
 *         // run the tool, then send back the assistant message and
 *         // {.role = "tool", .tool_name = call->function.name, .content = result}
 *     }
 *     ollama_response_free(res);
 * }
 * ollama_json_free(tool);
 * @endcode
 *
 * @param[in] name the function name, copied
 * @param[in] description what the function does, copied; NULL leaves it out
 * @return A new JSON object to release with ollama_json_free() (or to insert into an array of
 *         tools), or NULL if @p name is NULL or memory runs out.
 */
OLLAMA_API ollama_json_t *ollama_tool_new(const char *name, const char *description);
/**
 * @brief Adds a parameter to a tool built with ollama_tool_new().
 *
 * It sets `function.parameters.properties[name]` to an object with the "type" and "description"
 * given, replacing a parameter with the same name, and adds @p name to `function.parameters.required`
 * (once) when @p required is true. With @p required false the name is not removed from `required`:
 * a parameter added earlier as required stays required. It also works on a tool parsed from JSON
 * with the same structure.
 *
 * @code{.c}
 * ollama_json_t *unit = ollama_tool_add_parameter(tool, "unit", "string", "Temperature unit", false);
 * ollama_json_t *values = ollama_json_set(unit, "enum", ollama_json_array());
 * ollama_json_append(values, ollama_json_string("celsius"));
 * ollama_json_append(values, ollama_json_string("fahrenheit"));
 * @endcode
 *
 * @param[in,out] tool the tool
 * @param[in] name the parameter name, copied
 * @param[in] type a JSON schema type ("string", "integer", "number", "boolean", "array" or
 *            "object"); NULL leaves it out
 * @param[in] description what the parameter is; NULL leaves it out
 * @param[in] required true if the model must always pass the parameter
 * @return The schema object of the parameter, owned by the tool, so that more keywords such as
 *         "enum" or "items" can be added; NULL if @p tool has no `function.parameters.properties`
 *         object, @p name is NULL or memory runs out.
 */
OLLAMA_API ollama_json_t *ollama_tool_add_parameter(ollama_json_t *tool, const char *name, const char *type,
                                                    const char *description, bool required);

/**
 * @brief Encodes bytes in standard base64, with padding.
 * @param[in] data the bytes to encode; it may be NULL only when @p size is 0
 * @param[in] size number of bytes
 * @return A new NUL-terminated string to release with ollama_free(), or NULL if memory runs out.
 */
OLLAMA_API char *ollama_base64_encode(const void *data, size_t size);
/**
 * @brief Decodes base64 text, such as the image of an image generation response.
 *
 * Both the standard and the URL-safe alphabets are accepted, whitespace is ignored and the padding
 * is optional; any other character, or data after the padding, makes the decoding fail. The length
 * is not checked: trailing characters that do not complete a byte are ignored ("Q" decodes to 0
 * bytes).
 *
 * @param[in] text the NUL-terminated base64 text, or NULL
 * @param[out] size receives the number of decoded bytes, 0 on failure; may be NULL
 * @return The decoded bytes, followed by a NUL byte not counted in @p size, to release with
 *         ollama_free(); NULL if @p text is NULL or invalid, or if memory runs out.
 */
OLLAMA_API unsigned char *ollama_base64_decode(const char *text, size_t *size);

/**
 * @brief Releases memory returned by the library as a plain buffer.
 *
 * Use it for the strings of ollama_json_stringify() and ollama_create_blob() and for the results of
 * the base64 helpers. The other objects have their own function: ollama_json_free(),
 * ollama_response_free(), ollama_stream_free(), ollama_messages_free() and ollama_client_free().
 * Unlike free(), it also works when the library uses another C runtime than the application, as
 * can happen with Windows DLLs.
 *
 * @param[in] ptr the memory to release; NULL is ignored
 */
OLLAMA_API void ollama_free(void *ptr);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* OLLAMA_H */
