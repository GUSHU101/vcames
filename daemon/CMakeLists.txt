cmake_minimum_required(VERSION 3.22)
project(vcamesd LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

enable_testing()
add_executable(vcames_parser_tests tests/parser_test.cpp)
target_link_libraries(vcames_parser_tests PRIVATE vcames_parser)
if(MSVC)
    target_compile_options(vcames_parser_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(vcames_parser_tests PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME vcames_parser_tests COMMAND vcames_parser_tests)
