cmake_minimum_required(VERSION 3.22)
project(crose_pet VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(thirdparty/raylib EXCLUDE_FROM_ALL)

add_library(raygui_headers INTERFACE)
target_include_directories(raygui_headers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/raygui/src)
add_library(portable_file_dialogs_headers INTERFACE)
target_include_directories(portable_file_dialogs_headers INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/portable-file-dialogs)
add_library(nlohmann_json_headers INTERFACE)
add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json_headers)
target_include_directories(nlohmann_json_headers INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/nlohmann-json/include)

add_executable(crose_pet
    src/main.cpp
    src/configurator.cpp
    src/file_dialog.cpp
    src/model_render.cpp
    src/pet_config.cpp)
target_link_libraries(crose_pet PRIVATE raylib raygui_headers portable_file_dialogs_headers
    nlohmann_json::nlohmann_json)
if(WIN32)
    target_sources(crose_pet PRIVATE src/system_tray_win32.c)
    target_link_libraries(crose_pet PRIVATE shell32 user32 gdi32)
    # Windows 头文件默认把 min/max 定义成宏，会破坏 std::min / std::max。
    target_compile_definitions(crose_pet PRIVATE NOMINMAX)
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
configure_file(config/pet.json pet.json COPYONLY)

include(CTest)
if(BUILD_TESTING)
    add_executable(config_roundtrip_test tests/config_roundtrip.cpp src/pet_config.cpp)
    target_include_directories(config_roundtrip_test PRIVATE src)
    target_link_libraries(config_roundtrip_test PRIVATE nlohmann_json::nlohmann_json)
    add_test(NAME config_roundtrip COMMAND config_roundtrip_test)
endif()
