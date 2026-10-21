# libFuzzer targets. Build them with Clang:
#   CC=clang cmake -S . -B build-fuzz -DOLLAMA_BUILD_FUZZERS=ON
#   cmake --build build-fuzz --target fuzz_json fuzz_host
#   mkdir -p build-fuzz/corpus
#   ./build-fuzz/fuzz/fuzz_json -dict=fuzz/json.dict build-fuzz/corpus fuzz/corpus/json
include(CheckCSourceCompiles)
set(CMAKE_REQUIRED_FLAGS -fsanitize=fuzzer)
set(CMAKE_REQUIRED_LINK_OPTIONS -fsanitize=fuzzer)
check_c_source_compiles("
#include <stddef.h>
#include <stdint.h>
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) { (void)data; (void)size; return 0; }"
    OLLAMA_HAVE_LIBFUZZER)
unset(CMAKE_REQUIRED_FLAGS)
unset(CMAKE_REQUIRED_LINK_OPTIONS)
if(NOT OLLAMA_HAVE_LIBFUZZER)
    message(FATAL_ERROR "OLLAMA_BUILD_FUZZERS requires Clang with libFuzzer (-fsanitize=fuzzer)")
endif()

list(TRANSFORM OLLAMA_SOURCES PREPEND ${PROJECT_SOURCE_DIR}/ OUTPUT_VARIABLE OLLAMA_FUZZ_SOURCES)
set(OLLAMA_FUZZ_FLAGS -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all)

foreach(name json host)
    add_executable(fuzz_${name} fuzz_${name}.c ${OLLAMA_FUZZ_SOURCES})
    target_include_directories(fuzz_${name} PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(fuzz_${name} PRIVATE CURL::libcurl)
    target_compile_options(fuzz_${name} PRIVATE -g ${OLLAMA_FUZZ_FLAGS})
    target_link_options(fuzz_${name} PRIVATE ${OLLAMA_FUZZ_FLAGS})
    set_target_properties(fuzz_${name} PROPERTIES C_STANDARD ${OLLAMA_C_STANDARD} C_EXTENSIONS OFF)
endforeach()
