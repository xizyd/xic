cmake_minimum_required(VERSION 3.14)
project(Xi VERSION 1.0.0 LANGUAGES C CXX)

add_library(Xi 
    ${CMAKE_CURRENT_SOURCE_DIR}/packages/monocypher/monocypher.c
)

target_include_directories(Xi PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_include_directories(Xi PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/packages/monocypher
)

target_compile_features(Xi PUBLIC cxx_std_17)

add_library(Xi::Xi ALIAS Xi)
