cmake_minimum_required(VERSION 3.14)
project(asvjsonpp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/Zc:__cplusplus)
endif()

add_executable(examples examples.cpp)
target_include_directories(examples PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test test.cpp)
target_include_directories(test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
