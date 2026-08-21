cmake_minimum_required(VERSION 3.22)
project(vcames_app_native LANGUAGES CXX)

add_library(vcames_yuv SHARED
    yuv420_jni.cpp
    ../../../../native/yuv420_converter.cpp
)

target_include_directories(vcames_yuv PRIVATE ../../../../native)
target_compile_features(vcames_yuv PRIVATE cxx_std_20)
target_compile_options(vcames_yuv PRIVATE -Wall -Wextra -Werror)
