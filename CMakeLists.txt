cmake_minimum_required(VERSION 3.22)
project(crose_pet VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 02566453880e06bec97f5b8160fa6fe1e98397ca
    GIT_SHALLOW TRUE)
FetchContent_Declare(raygui
    GIT_REPOSITORY https://github.com/raysan5/raygui.git
    GIT_TAG 25c8c65a6e5f0f4d4b564a0343861898c6f2778b
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR cmake-unused)
FetchContent_Declare(portable_file_dialogs
    GIT_REPOSITORY https://github.com/samhocevar/portable-file-dialogs.git
    GIT_TAG 8ccf2a93f70d021842e72fd950eac2a0bb244eec
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR cmake-unused)
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(raylib raygui portable_file_dialogs nlohmann_json)

add_library(portable_file_dialogs_headers INTERFACE)
target_include_directories(portable_file_dialogs_headers INTERFACE ${portable_file_dialogs_SOURCE_DIR})

add_executable(crose_pet
    src/main.cpp
    src/configurator.cpp
    src/model_render.cpp
    src/pet_config.cpp)
target_include_directories(crose_pet PRIVATE ${raygui_SOURCE_DIR}/src)
target_link_libraries(crose_pet PRIVATE raylib portable_file_dialogs_headers nlohmann_json::nlohmann_json)
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
configure_file(config/pet.json pet.json COPYONLY)

include(CTest)
if(BUILD_TESTING)
    add_executable(config_roundtrip_test tests/config_roundtrip.cpp src/pet_config.cpp)
    target_include_directories(config_roundtrip_test PRIVATE src)
    target_link_libraries(config_roundtrip_test PRIVATE nlohmann_json::nlohmann_json)
    add_test(NAME config_roundtrip COMMAND config_roundtrip_test)
endif()
