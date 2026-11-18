cmake_minimum_required(VERSION 3.20)
project(DungeonCards LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/utf-8 /W4)   # исходники в UTF-8 (кириллица в строках)
else()
  add_compile_options(-Wall -Wextra)
endif()

option(DC_BUILD_GAME "Собрать графическую игру (нужен raylib)" ON)
option(DC_BUILD_TOOLS "Собрать тесты и симулятор баланса" ON)

# --- ядро игры: чистая логика без графики -----------------------------------
add_library(dc_core STATIC
  src/core/Assets.cpp
  src/core/Boss.cpp
  src/core/Meta.cpp
  src/core/Profile.cpp
  src/core/Card.cpp
  src/core/Dungeon.cpp
  src/core/Party.cpp
  src/core/Combat.cpp
  src/core/Run.cpp)
target_include_directories(dc_core PUBLIC src)

# --- графическая игра (raylib) ----------------------------------------------
if(DC_BUILD_GAME)
  find_package(raylib 5.0 QUIET)
  if(NOT raylib_FOUND)
    include(FetchContent)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(raylib
      GIT_REPOSITORY https://github.com/raysan5/raylib.git
      GIT_TAG 5.5
      GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(raylib)
  endif()

  add_executable(DungeonCards src/main.cpp src/game/Game.cpp)
  target_link_libraries(DungeonCards PRIVATE dc_core raylib)

  # assets/ рядом с exe (там можно положить font.ttf с кириллицей)
  add_custom_command(TARGET DungeonCards POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:DungeonCards>/assets)
endif()

# --- тесты и симулятор -------------------------------------------------------
if(DC_BUILD_TOOLS)
  enable_testing()
  add_executable(core_tests tests/core_tests.cpp)
  target_link_libraries(core_tests PRIVATE dc_core)
  add_test(NAME core_tests COMMAND core_tests)

  add_executable(list_assets tools/list_assets.cpp)
  target_link_libraries(list_assets PRIVATE dc_core)

  add_executable(balance_sim tools/sim.cpp)
  target_link_libraries(balance_sim PRIVATE dc_core)
endif()
