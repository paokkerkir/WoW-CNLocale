cmake_minimum_required(VERSION 3.20)
project(CNLocale VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_library(CNLocale SHARED
    src/dllmain.cpp
    src/locale_hooks.cpp
    src/registry_hooks.cpp
    src/clip_hook.cpp
    src/CNLocale.def
)

target_include_directories(CNLocale PRIVATE
    src
    third_party
)

target_link_libraries(CNLocale PRIVATE
    kernel32
    advapi32
    user32
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/MinHook.x86.lib
)

if(MSVC)
    # Static CRT — no extra runtime DLL dependencies
    set_property(TARGET CNLocale PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(CNLocale PRIVATE /W4 /permissive-)
endif()

set_target_properties(CNLocale PROPERTIES DEBUG_POSTFIX "_d")
