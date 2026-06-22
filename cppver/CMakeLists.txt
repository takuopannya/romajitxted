cmake_minimum_required(VERSION 3.20)
project(RomajiTxted VERSION 1.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Unicode build
add_definitions(-DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX)

# Sources
set(SOURCES
    src/main.cpp
    src/app.cpp
    src/ui.cpp
    src/http.cpp
    src/llm.cpp
    src/crypto.cpp
    src/settings.cpp
    src/resource.rc
)

add_executable(RomajiTxted WIN32 ${SOURCES})

target_include_directories(RomajiTxted PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Windows system libraries
target_link_libraries(RomajiTxted PRIVATE
    winhttp
    bcrypt
    comctl32
    uxtheme
    gdi32
    user32
    shell32
    comdlg32
    advapi32
)

# MSVC specific flags
if(MSVC)
    target_compile_options(RomajiTxted PRIVATE /utf-8 /W3)
    # Static CRT for standalone exe
    set_property(TARGET RomajiTxted PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# MinGW specific
if(MINGW)
    target_compile_options(RomajiTxted PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
    target_link_options(RomajiTxted PRIVATE -static -mwindows -municode)
endif()
