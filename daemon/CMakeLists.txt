cmake_minimum_required(VERSION 3.22)
project(vcamesd LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(ANDROID)
    add_executable(vcamesd
        src/config.cpp
        src/engine.cpp
        src/http_mjpeg_source.cpp
        src/image_transform.cpp
        src/main.cpp
        src/mjpeg_parser.cpp
        src/replacement_adapter.cpp
        src/v4l2_sink.cpp
    )
    target_include_directories(vcamesd PRIVATE include third_party/stb)
    target_compile_options(vcamesd PRIVATE -Wall -Wextra -Werror)
    find_library(ANDROID_LOG_LIBRARY log REQUIRED)
    target_link_libraries(vcamesd PRIVATE ${ANDROID_LOG_LIBRARY})
else()
    add_library(vcames_parser STATIC
        src/config.cpp
        src/mjpeg_parser.cpp
    )
    target_include_directories(vcames_parser PUBLIC include)
    if(MSVC)
        target_compile_options(vcames_parser PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(vcames_parser PRIVATE -Wall -Wextra -Werror)
    endif()

    if(UNIX)
        add_library(vcames_adapter STATIC src/replacement_adapter.cpp)
        target_include_directories(vcames_adapter PRIVATE include)
        target_compile_options(vcames_adapter PRIVATE -Wall -Wextra -Werror)
    endif()

    enable_testing()
    add_executable(vcames_parser_tests tests/parser_test.cpp)
    target_link_libraries(vcames_parser_tests PRIVATE vcames_parser)
    if(MSVC)
        target_compile_options(vcames_parser_tests PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(vcames_parser_tests PRIVATE -Wall -Wextra -Werror)
    endif()
    add_test(NAME vcames_parser_tests COMMAND vcames_parser_tests)
endif()
