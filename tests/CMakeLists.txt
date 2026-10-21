# Unit tests compile the sources directly to reach the internal functions.
list(TRANSFORM OLLAMA_SOURCES PREPEND ${PROJECT_SOURCE_DIR}/ OUTPUT_VARIABLE OLLAMA_TEST_SOURCES)
add_executable(test_unit test_unit.c ${OLLAMA_TEST_SOURCES})
target_include_directories(test_unit PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(test_unit PRIVATE CURL::libcurl)
target_compile_options(test_unit PRIVATE ${OLLAMA_WARNINGS})
set_target_properties(test_unit PROPERTIES C_STANDARD 99 C_EXTENSIONS OFF)
add_test(NAME unit COMMAND test_unit)

# Integration tests use the public API against a fake Ollama server.
add_executable(test_client test_client.c)
target_link_libraries(test_client PRIVATE ollama::ollama CURL::libcurl)
target_compile_options(test_client PRIVATE ${OLLAMA_WARNINGS})
set_target_properties(test_client PROPERTIES C_STANDARD 99 C_EXTENSIONS OFF)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME client
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/mock_server.py $<TARGET_FILE:test_client>)
else()
    message(STATUS "Python 3 not found: the client integration test is disabled")
endif()
