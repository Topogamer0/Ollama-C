cmake_minimum_required(VERSION 3.14)

project(ollama-c
    VERSION 0.1.0
    DESCRIPTION "C client library for Ollama, a port of ollama-python"
    LANGUAGES C)

include(GNUInstallDirs)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(OLLAMA_TOP_LEVEL ON)
else()
    set(OLLAMA_TOP_LEVEL OFF)
endif()

option(BUILD_SHARED_LIBS "Build ollama-c as a shared library" OFF)
option(OLLAMA_BUILD_EXAMPLES "Build the examples" ${OLLAMA_TOP_LEVEL})
option(OLLAMA_BUILD_TESTS "Build the tests" ${OLLAMA_TOP_LEVEL})
option(OLLAMA_INSTALL "Generate the install target" ${OLLAMA_TOP_LEVEL})

find_package(CURL REQUIRED)

set(OLLAMA_SOURCES
    src/api.c
    src/client.c
    src/helpers.c
    src/host.c
    src/http.c
    src/json.c
    src/request.c
    src/response.c
    src/util.c)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set(OLLAMA_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes)
elseif(MSVC)
    set(OLLAMA_WARNINGS /W4)
endif()

add_library(ollama ${OLLAMA_SOURCES})
add_library(ollama::ollama ALIAS ollama)
target_include_directories(ollama PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(ollama PRIVATE CURL::libcurl)
target_compile_options(ollama PRIVATE ${OLLAMA_WARNINGS})
if(MSVC)
    target_compile_definitions(ollama PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
set_target_properties(ollama PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    C_VISIBILITY_PRESET hidden
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

if(OLLAMA_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(OLLAMA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(OLLAMA_INSTALL)
    include(CMakePackageConfigHelpers)

    install(TARGETS ollama EXPORT ollama-c-targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES include/ollama.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(FILES LICENSE README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
    install(EXPORT ollama-c-targets
        NAMESPACE ollama::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ollama-c)

    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/ollama-c-config.cmake
        "include(CMakeFindDependencyMacro)\n"
        "find_dependency(CURL)\n"
        "include(\"\${CMAKE_CURRENT_LIST_DIR}/ollama-c-targets.cmake\")\n")
    write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/ollama-c-config-version.cmake
        COMPATIBILITY SameMinorVersion)
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/ollama-c-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/ollama-c-config-version.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ollama-c)

    # relative to the .pc file, so that the install can be relocated
    if(IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}")
        set(OLLAMA_PC_PREFIX "${CMAKE_INSTALL_PREFIX}")
    else()
        file(RELATIVE_PATH OLLAMA_PC_RELATIVE "/${CMAKE_INSTALL_LIBDIR}/pkgconfig" "/")
        set(OLLAMA_PC_PREFIX "\${pcfiledir}/${OLLAMA_PC_RELATIVE}")
    endif()
    # a static libollama needs libcurl on the link line of its users
    if(BUILD_SHARED_LIBS)
        set(OLLAMA_PC_REQUIRES "Requires.private")
    else()
        set(OLLAMA_PC_REQUIRES "Requires")
    endif()
    configure_file(ollama-c.pc.in ${CMAKE_CURRENT_BINARY_DIR}/ollama-c.pc @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ollama-c.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
endif()
