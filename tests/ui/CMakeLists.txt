# BlockPlanet's menu tests on their own, without the ROM or generated game
# code (the full build, CMakeLists.txt at the top, needs both):
#
#   cmake -S tests/ui -B build-ui && cmake --build build-ui && ctest --test-dir build-ui
#   cmake --build build-ui --target ui_capture   (captures of the menus, for review)
#
# Needs RmlUi and FreeType in .local/deps (tools/build/fetch_dependencies.py).
cmake_minimum_required(VERSION 3.20)
project(BlockPlanetUiTests CXX C)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()
get_filename_component(BLOCKPLANET_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
include("${BLOCKPLANET_SOURCE_DIR}/cmake/BlockPlanetUI.cmake")
