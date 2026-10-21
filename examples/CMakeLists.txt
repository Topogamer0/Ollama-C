set(OLLAMA_EXAMPLES
    generate
    generate-stream
    chat
    chat-stream
    chat-with-history
    tools
    structured-outputs
    thinking
    multimodal
    embed
    list
    show
    ps
    pull
    create
    web-search)

foreach(name IN LISTS OLLAMA_EXAMPLES)
    add_executable(example-${name} ${name}.c)
    target_link_libraries(example-${name} PRIVATE ollama::ollama)
    target_compile_options(example-${name} PRIVATE ${OLLAMA_WARNINGS})
    set_target_properties(example-${name} PROPERTIES OUTPUT_NAME ${name} C_STANDARD 99 C_EXTENSIONS OFF)
endforeach()
