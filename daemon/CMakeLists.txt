cmake_minimum_required(VERSION 3.22)
project(vcamesd LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/EHsc)
endif()

if(ANDROID)
    add_executable(vcamesd
        src/config.cpp
        src/engine.cpp
        src/ffmpeg_source.cpp
        src/image_transform.cpp
        src/main.cpp
        src/mjpeg_parser.cpp
        src/stream_source.cpp
        src/v4l2_sink.cpp
    )
    target_include_directories(vcamesd PRIVATE include third_party/stb)
    target_compile_options(vcamesd PRIVATE -Wall -Wextra -Werror)
    find_library(ANDROID_LOG_LIBRARY log REQUIRED)
    target_link_libraries(vcamesd PRIVATE ${ANDROID_LOG_LIBRARY})

    add_executable(vcames-socket-proxy src/socket_proxy.cpp)
    target_compile_options(vcames-socket-proxy PRIVATE -Wall -Wextra -Werror)
else()
    enable_testing()
    add_library(vcames_parser STATIC
        src/config.cpp
        src/mjpeg_parser.cpp
        src/stream_source.cpp
    )
    target_include_directories(vcames_parser PUBLIC include)
    if(MSVC)
        target_compile_options(vcames_parser PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(vcames_parser PRIVATE -Wall -Wextra -Werror)
    endif()

    add_library(vcames_image_transform STATIC src/image_transform.cpp)
    target_include_directories(vcames_image_transform PRIVATE include third_party/stb)
    add_executable(vcames_image_transform_tests tests/image_transform_test.cpp)
    target_link_libraries(vcames_image_transform_tests PRIVATE vcames_image_transform)
    target_include_directories(vcames_image_transform_tests PRIVATE include)
    if(MSVC)
        target_compile_options(vcames_image_transform PRIVATE /W3)
        target_compile_options(vcames_image_transform_tests PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(vcames_image_transform PRIVATE -Wall -Wextra -Werror)
        target_compile_options(vcames_image_transform_tests PRIVATE -Wall -Wextra -Werror)
    endif()
    add_test(NAME vcames_image_transform_tests COMMAND vcames_image_transform_tests)

    add_library(vcames_yuv420 STATIC ../native/yuv420_converter.cpp)
    target_include_directories(vcames_yuv420 PUBLIC ../native)
    if(MSVC)
        target_compile_options(vcames_yuv420 PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(vcames_yuv420 PRIVATE -Wall -Wextra -Werror)
    endif()
    add_executable(vcames_yuv420_tests tests/yuv420_converter_test.cpp)
    target_link_libraries(vcames_yuv420_tests PRIVATE vcames_yuv420)
    if(MSVC)
        target_compile_options(vcames_yuv420_tests PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(vcames_yuv420_tests PRIVATE -Wall -Wextra -Werror)
    endif()
    add_test(NAME vcames_yuv420_tests COMMAND vcames_yuv420_tests)

    add_executable(vcames_parser_tests tests/parser_test.cpp)
    target_link_libraries(vcames_parser_tests PRIVATE vcames_parser)
    if(MSVC)
        target_compile_options(vcames_parser_tests PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(vcames_parser_tests PRIVATE -Wall -Wextra -Werror)
    endif()
    add_test(NAME vcames_parser_tests COMMAND vcames_parser_tests)

    add_executable(vcames_stream_source_tests tests/stream_source_test.cpp)
    target_link_libraries(vcames_stream_source_tests PRIVATE vcames_parser)
    if(MSVC)
        target_compile_options(vcames_stream_source_tests PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(vcames_stream_source_tests PRIVATE -Wall -Wextra -Werror)
    endif()
    add_test(NAME vcames_stream_source_tests COMMAND vcames_stream_source_tests)
endif()
