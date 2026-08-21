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
        src/http_mjpeg_source.cpp
        src/image_transform.cpp
        src/main.cpp
        src/mjpeg_parser.cpp
        src/network_policy.cpp
        src/replacement_adapter.cpp
        src/shared_frame_bus.cpp
    )
    target_include_directories(vcamesd PRIVATE include third_party/stb)
    target_compile_options(vcamesd PRIVATE -Wall -Wextra -Werror)
    find_library(ANDROID_LOG_LIBRARY log REQUIRED)
    target_link_libraries(vcamesd PRIVATE ${ANDROID_LOG_LIBRARY})

    add_executable(vcames-socket-proxy src/socket_proxy.cpp)
    target_compile_options(vcames-socket-proxy PRIVATE -Wall -Wextra -Werror)
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

    add_library(vcames_network_policy STATIC src/network_policy.cpp)
    target_include_directories(vcames_network_policy PUBLIC include)
    if(MSVC)
        target_compile_options(vcames_network_policy PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(vcames_network_policy PRIVATE -Wall -Wextra -Werror)
    endif()
    add_executable(vcames_network_policy_tests tests/network_policy_test.cpp)
    target_link_libraries(vcames_network_policy_tests PRIVATE vcames_network_policy)
    if(MSVC)
        target_compile_options(vcames_network_policy_tests PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(vcames_network_policy_tests PRIVATE -Wall -Wextra -Werror)
    endif()
    add_test(NAME vcames_network_policy_tests COMMAND vcames_network_policy_tests)

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

    if(UNIX AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_package(Threads REQUIRED)
        add_library(vcames_adapter STATIC src/replacement_adapter.cpp)
        target_include_directories(vcames_adapter PRIVATE include)
        target_compile_options(vcames_adapter PRIVATE -Wall -Wextra -Werror)

        add_library(vcames_frame_bus STATIC src/shared_frame_bus.cpp)
        target_include_directories(vcames_frame_bus PUBLIC include)
        target_compile_options(vcames_frame_bus PRIVATE -Wall -Wextra -Werror)

        add_executable(vcames_frame_bus_tests tests/shared_frame_bus_test.cpp)
        target_link_libraries(vcames_frame_bus_tests PRIVATE vcames_frame_bus)
        target_compile_options(vcames_frame_bus_tests PRIVATE -Wall -Wextra -Werror)
        add_test(NAME vcames_frame_bus_tests COMMAND vcames_frame_bus_tests)

        add_executable(vcames_adapter_integration_tests
            tests/replacement_adapter_integration_test.cpp)
        target_link_libraries(vcames_adapter_integration_tests PRIVATE
            vcames_adapter vcames_frame_bus Threads::Threads)
        target_compile_options(vcames_adapter_integration_tests PRIVATE -Wall -Wextra -Werror)
        add_test(NAME vcames_adapter_integration_tests
            COMMAND vcames_adapter_integration_tests)
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
