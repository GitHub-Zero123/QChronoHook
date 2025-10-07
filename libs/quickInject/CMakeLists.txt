cmake_minimum_required(VERSION 3.20)

# By Zero123
project(QUICK_INJECT)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    if(MSVC)
        add_compile_options(/utf-8 /EHsc)
    endif()
endif()

add_library(quickInject OBJECT)

# windows 实现
if(WIN32)
    target_sources(quickInject PRIVATE src/winInject.cpp)
endif()

target_compile_features(quickInject PRIVATE cxx_std_20)

target_include_directories(quickInject PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)