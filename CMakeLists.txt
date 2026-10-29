cmake_minimum_required(VERSION 3.20)
project(crose_pet VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 02566453880e06bec97f5b8160fa6fe1e98397ca
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(raylib)

add_executable(crose_pet src/main.cpp)
target_link_libraries(crose_pet PRIVATE raylib)
if(WIN32)
    target_sources(crose_pet PRIVATE src/system_tray_win32.c)
    target_link_libraries(crose_pet PRIVATE shell32 user32 gdi32)
else()
    target_sources(crose_pet PRIVATE src/system_tray_stub.c)
endif()
if(MINGW)
    target_link_options(crose_pet PRIVATE -mwindows)
endif()
if(MSVC)
    target_compile_options(crose_pet PRIVATE /W4 /utf-8)
else()
    target_compile_options(crose_pet PRIVATE -Wall -Wextra -Wpedantic)
endif()
configure_file(config/pet.ini pet.ini COPYONLY)
