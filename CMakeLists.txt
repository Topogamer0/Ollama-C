cmake_minimum_required(VERSION 3.14...3.31)

project(ollama-c
    VERSION 0.2.0
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
option(OLLAMA_BUILD_FUZZERS "Build the libFuzzer targets (requires Clang)" OFF)
option(OLLAMA_WERROR "Treat compiler warnings as errors" OFF)

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

# MSVC has no C99 mode: C11 is the oldest standard it implements, and in its
# default mode /W4 reports C99 features such as non-constant initializers.
if(MSVC)
    set(OLLAMA_C_STANDARD 11)
    set(OLLAMA_C_STANDARD_REQUIRED OFF)
else()
    set(OLLAMA_C_STANDARD 99)
    set(OLLAMA_C_STANDARD_REQUIRED ON)
endif()

if(MSVC)
    # /utf-8: sources are UTF-8; /external: no warnings from the Windows SDK and other system headers
    set(OLLAMA_WARNINGS /W4 /utf-8 /external:anglebrackets /external:W0)
    if(OLLAMA_WERROR)
        list(APPEND OLLAMA_WARNINGS /WX)
    endif()
    # fopen(), getenv() and the like are standard C, not "unsafe" functions
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set(OLLAMA_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes)
    if(OLLAMA_WERROR)
        list(APPEND OLLAMA_WARNINGS -Werror)
    endif()
endif()

add_library(ollama ${OLLAMA_SOURCES})
add_library(ollama::ollama ALIAS ollama)
target_include_directories(ollama PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(ollama PRIVATE CURL::libcurl)
target_compile_options(ollama PRIVATE ${OLLAMA_WARNINGS})
if(BUILD_SHARED_LIBS)
    # exports the functions marked OLLAMA_API from the DLL on Windows
    target_compile_definitions(ollama PRIVATE OLLAMA_BUILDING_DLL)
endif()
set_target_properties(ollama PROPERTIES
    C_STANDARD ${OLLAMA_C_STANDARD}
    C_STANDARD_REQUIRED ${OLLAMA_C_STANDARD_REQUIRED}
    C_EXTENSIONS OFF
    C_VISIBILITY_PRESET hidden
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

if(OLLAMA_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(OLLAMA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(OLLAMA_BUILD_FUZZERS)
    add_subdirectory(fuzz)
endif()

# `cmake --build build --target docs` writes the API reference to build/docs/html
if(OLLAMA_TOP_LEVEL)
    find_package(Doxygen QUIET)
    if(DOXYGEN_FOUND)
        configure_file(docs/Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)
        add_custom_target(docs
            COMMAND Doxygen::doxygen ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Generating the API documentation"
            VERBATIM)
    endif()
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
        COMPATIBILITY SameMajorVersion)
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/ollama-c-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/ollama-c-config-version.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ollama-c)

    # relative to the .pc file, so that the install can be relocated; the
    # paths are built on the real prefix because "/..." is not absolute on Windows
    if(IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}")
        set(OLLAMA_PC_PREFIX "${CMAKE_INSTALL_PREFIX}")
    else()
        file(RELATIVE_PATH OLLAMA_PC_RELATIVE
            "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/pkgconfig" "${CMAKE_INSTALL_PREFIX}")
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
