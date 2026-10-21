# Unit tests compile the sources directly to reach the internal functions.
list(TRANSFORM OLLAMA_SOURCES PREPEND ${PROJECT_SOURCE_DIR}/ OUTPUT_VARIABLE OLLAMA_TEST_SOURCES)
add_executable(test_unit test_unit.c ${OLLAMA_TEST_SOURCES})
target_include_directories(test_unit PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(test_unit PRIVATE CURL::libcurl)
target_compile_options(test_unit PRIVATE ${OLLAMA_WARNINGS})
set_target_properties(test_unit PROPERTIES C_STANDARD ${OLLAMA_C_STANDARD} C_EXTENSIONS OFF)
add_test(NAME unit COMMAND test_unit)

# The fuzz targets, replayed on the corpus and on the inputs of past crashes
# with any compiler, so that the bugs found by fuzzing stay fixed.
add_executable(fuzz_replay
    ${PROJECT_SOURCE_DIR}/fuzz/fuzz_json.c ${PROJECT_SOURCE_DIR}/fuzz/standalone.c ${OLLAMA_TEST_SOURCES})
target_include_directories(fuzz_replay PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(fuzz_replay PRIVATE CURL::libcurl)
target_compile_options(fuzz_replay PRIVATE ${OLLAMA_WARNINGS})
set_target_properties(fuzz_replay PROPERTIES C_STANDARD ${OLLAMA_C_STANDARD} C_EXTENSIONS OFF)
file(GLOB OLLAMA_FUZZ_INPUTS CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/fuzz/corpus/json/*
    ${PROJECT_SOURCE_DIR}/fuzz/regressions/json-*
    ${PROJECT_SOURCE_DIR}/fuzz/regressions/stream-*)
add_test(NAME fuzz_replay COMMAND fuzz_replay ${OLLAMA_FUZZ_INPUTS})

add_executable(fuzz_host_replay
    ${PROJECT_SOURCE_DIR}/fuzz/fuzz_host.c ${PROJECT_SOURCE_DIR}/fuzz/standalone.c ${OLLAMA_TEST_SOURCES})
target_include_directories(fuzz_host_replay PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(fuzz_host_replay PRIVATE CURL::libcurl)
target_compile_options(fuzz_host_replay PRIVATE ${OLLAMA_WARNINGS})
set_target_properties(fuzz_host_replay PROPERTIES C_STANDARD ${OLLAMA_C_STANDARD} C_EXTENSIONS OFF)
file(GLOB OLLAMA_FUZZ_HOST_INPUTS CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/fuzz/corpus/host/*
    ${PROJECT_SOURCE_DIR}/fuzz/regressions/host-*)
add_test(NAME fuzz_host_replay COMMAND fuzz_host_replay ${OLLAMA_FUZZ_HOST_INPUTS})

# Integration tests use the public API against a fake Ollama server.
add_executable(test_client test_client.c)
target_link_libraries(test_client PRIVATE ollama::ollama CURL::libcurl)
target_compile_options(test_client PRIVATE ${OLLAMA_WARNINGS})
set_target_properties(test_client PROPERTIES C_STANDARD ${OLLAMA_C_STANDARD} C_EXTENSIONS OFF)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME client
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/mock_server.py $<TARGET_FILE:test_client>)
else()
    message(STATUS "Python 3 not found: the client integration test is disabled")
endif()
