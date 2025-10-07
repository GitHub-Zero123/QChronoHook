cmake_minimum_required(VERSION 3.11)

# By Zero123
project(CHRONO_HOOK LANGUAGES CXX C)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    if(MSVC)
        add_compile_options(/utf-8 /EHsc)
    endif()
endif()

add_subdirectory(libs/qpyipc)       # Python IPC通信
add_subdirectory(libs/quickInject)  # DLL注入
add_subdirectory(libs/minhook)      # API Hook

add_executable(chronoApi chronoApi.cpp)
target_compile_features(chronoApi PRIVATE cxx_std_20)
target_link_libraries(chronoApi PRIVATE quickInject pyipc)

if(MSVC)
    # 仅针对release静态链接运行时库
    set_property(TARGET chronoApi PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endif()

add_library(chronoHook SHARED chronoHook.cpp)
target_compile_features(chronoHook PRIVATE cxx_std_20)
target_link_libraries(chronoHook PRIVATE minhook)

# 保证在构建 chronoApi 之前先构建 chronoHook
add_dependencies(chronoApi chronoHook)