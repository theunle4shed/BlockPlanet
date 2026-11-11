# BlockPlanet's menu layer (docs/UI.md): RmlUi with FreeType, drawn through
# RT64. Both are built from the pinned sources in .local/deps.
#
# The menu tests (and the capture tool, ui_capture) build without the ROM or
# generated game code through tests/ui/CMakeLists.txt.

if(NOT BLOCKPLANET_SOURCE_DIR)
    set(BLOCKPLANET_SOURCE_DIR "${CMAKE_SOURCE_DIR}")
endif()

set(BLOCKPLANET_RMLUI_ROOT "${BLOCKPLANET_SOURCE_DIR}/.local/deps/RmlUi")
set(BLOCKPLANET_FREETYPE_ROOT "${BLOCKPLANET_SOURCE_DIR}/.local/deps/freetype")
if(NOT EXISTS "${BLOCKPLANET_RMLUI_ROOT}/CMakeLists.txt" OR NOT EXISTS "${BLOCKPLANET_FREETYPE_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "RmlUi and FreeType are missing: run tools/build/fetch_dependencies.py --root .local/deps --name RmlUi (and --name freetype)")
endif()

# FreeType: static, without optional compression and shaping libraries.
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(BLOCKPLANET_SAVED_SHARED "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS OFF)
add_subdirectory("${BLOCKPLANET_FREETYPE_ROOT}" "${CMAKE_BINARY_DIR}/freetype" EXCLUDE_FROM_ALL)
# RmlUi finds FreeType with find_package(Freetype); point it at this build.
add_library(Freetype::Freetype ALIAS freetype)
set(FREETYPE_INCLUDE_DIR_ft2build "${BLOCKPLANET_FREETYPE_ROOT}/include" CACHE PATH "" FORCE)
set(FREETYPE_INCLUDE_DIR_freetype2 "${BLOCKPLANET_FREETYPE_ROOT}/include" CACHE PATH "" FORCE)
set(FREETYPE_LIBRARY freetype CACHE STRING "" FORCE)

# RmlUi: static library only.
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_FONT_ENGINE "freetype" CACHE STRING "" FORCE)
set(RMLUI_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
set(RMLUI_LUA_BINDINGS OFF CACHE BOOL "" FORCE)
add_subdirectory("${BLOCKPLANET_RMLUI_ROOT}" "${CMAKE_BINARY_DIR}/RmlUi" EXCLUDE_FROM_ALL)
set(BUILD_SHARED_LIBS "${BLOCKPLANET_SAVED_SHARED}")

# Shaders, compiled with RT64's DXC into C arrays (blockplanet_ui_shader).
if(WIN32)
    set(BLOCKPLANET_DXC "${RT64_ROOT}/src/contrib/dxc/bin/x64/dxc.exe")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(BLOCKPLANET_DXC "LD_LIBRARY_PATH=${RT64_ROOT}/src/contrib/dxc/lib/arm64" "${RT64_ROOT}/src/contrib/dxc/bin/arm64/dxc-linux")
else()
    set(BLOCKPLANET_DXC "LD_LIBRARY_PATH=${RT64_ROOT}/src/contrib/dxc/lib/x64" "${RT64_ROOT}/src/contrib/dxc/bin/x64/dxc-linux")
endif()
set(BLOCKPLANET_UI_SHADER_DIR "${CMAKE_BINARY_DIR}/ui-shaders")
file(MAKE_DIRECTORY "${BLOCKPLANET_UI_SHADER_DIR}")

function(blockplanet_ui_shader TARGET NAME PROFILE ENTRY)
    set(SOURCE "${BLOCKPLANET_SOURCE_DIR}/src/ui/shaders/${NAME}.hlsl")
    set(OUT "${BLOCKPLANET_UI_SHADER_DIR}/${NAME}.hlsl")
    add_custom_command(OUTPUT "${OUT}.spv"
        COMMAND ${BLOCKPLANET_DXC} -spirv -fspv-target-env=vulkan1.0 -fvk-use-dx-layout
                -E ${ENTRY} -T ${PROFILE} "${SOURCE}" /Fo "${OUT}.spv"
        DEPENDS "${SOURCE}")
    add_custom_command(OUTPUT "${OUT}.spirv.c" "${OUT}.spirv.h"
        COMMAND file_to_c "${OUT}.spv" ${NAME}BlobSPIRV "${OUT}.spirv.c" "${OUT}.spirv.h"
        DEPENDS "${OUT}.spv" file_to_c)
    target_sources(${TARGET} PRIVATE "${OUT}.spirv.c")
    if(WIN32)
        add_custom_command(OUTPUT "${OUT}.dxil"
            COMMAND ${BLOCKPLANET_DXC} -Wno-ignored-attributes -E ${ENTRY} -T ${PROFILE} "${SOURCE}" /Fo "${OUT}.dxil"
            DEPENDS "${SOURCE}")
        add_custom_command(OUTPUT "${OUT}.dxil.c" "${OUT}.dxil.h"
            COMMAND file_to_c "${OUT}.dxil" ${NAME}BlobDXIL "${OUT}.dxil.c" "${OUT}.dxil.h"
            DEPENDS "${OUT}.dxil" file_to_c)
        target_sources(${TARGET} PRIVATE "${OUT}.dxil.c")
    endif()
endfunction()

function(blockplanet_add_ui TARGET)
    target_sources(${TARGET} PRIVATE
        "${BLOCKPLANET_SOURCE_DIR}/src/ui/grid_texture.cpp"
        "${BLOCKPLANET_SOURCE_DIR}/src/ui/ui_grid.cpp"
        "${BLOCKPLANET_SOURCE_DIR}/src/ui/ui_navigator.cpp"
        "${BLOCKPLANET_SOURCE_DIR}/src/ui/ui_renderer.cpp"
        "${BLOCKPLANET_SOURCE_DIR}/src/ui/ui_system.cpp"
        "${BLOCKPLANET_SOURCE_DIR}/src/platform/menu_input.cpp"
        "${BLOCKPLANET_SOURCE_DIR}/src/platform/mode_launcher.cpp")
    blockplanet_ui_shader(${TARGET} UiVS vs_6_3 VSMain)
    blockplanet_ui_shader(${TARGET} UiPS ps_6_3 PSMain)
    target_include_directories(${TARGET} PRIVATE
        "${BLOCKPLANET_SOURCE_DIR}/src/ui"
        "${BLOCKPLANET_UI_SHADER_DIR}"
        "${RT64_ROOT}/src/contrib/plume"
        "${RT64_ROOT}/src/contrib/stb")
    target_link_libraries(${TARGET} PRIVATE RmlUi::RmlUi)
    # Menu documents and fonts beside the executable, in ui/. A separate
    # step, so editing a document alone updates the copy.
    add_custom_target(${TARGET}_ui_assets
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${BLOCKPLANET_SOURCE_DIR}/assets/ui" "$<TARGET_FILE_DIR:${TARGET}>/ui"
        COMMENT "Copying menu documents and fonts")
    add_dependencies(${TARGET} ${TARGET}_ui_assets)
endfunction()

# Navigator logic test (RmlUi with a renderer that draws nothing).
add_executable(ui_navigator_test "${BLOCKPLANET_SOURCE_DIR}/tests/ui_navigator_test.cpp"
    "${BLOCKPLANET_SOURCE_DIR}/src/ui/ui_navigator.cpp")
target_include_directories(ui_navigator_test PRIVATE "${BLOCKPLANET_SOURCE_DIR}/src/ui" "${BLOCKPLANET_SOURCE_DIR}/include")
target_link_libraries(ui_navigator_test PRIVATE RmlUi::RmlUi)
if(MSVC)
    target_compile_options(ui_navigator_test PRIVATE /UNDEBUG)
else()
    target_compile_options(ui_navigator_test PRIVATE -UNDEBUG)
endif()
add_test(NAME ui_navigator COMMAND ui_navigator_test "${CMAKE_CURRENT_BINARY_DIR}/ui-navigator-test")

# The menu grid: texture, layout and colour changes (no ROM needed).
add_executable(ui_grid_test "${BLOCKPLANET_SOURCE_DIR}/tests/ui_grid_test.cpp"
    "${BLOCKPLANET_SOURCE_DIR}/src/ui/grid_texture.cpp"
    "${BLOCKPLANET_SOURCE_DIR}/src/ui/ui_grid.cpp")
target_include_directories(ui_grid_test PRIVATE "${BLOCKPLANET_SOURCE_DIR}/src/ui")
target_link_libraries(ui_grid_test PRIVATE RmlUi::RmlUi)
if(MSVC)
    target_compile_options(ui_grid_test PRIVATE /UNDEBUG)
else()
    target_compile_options(ui_grid_test PRIVATE -UNDEBUG)
endif()
add_test(NAME ui_grid COMMAND ui_grid_test "${BLOCKPLANET_SOURCE_DIR}/assets/ui/grid.rml")

# Captures of the menu screens over the grid, drawn in software, for review
# (not a test): ui_capture <assets/ui> <output folder> [ROM].
add_executable(ui_capture EXCLUDE_FROM_ALL "${BLOCKPLANET_SOURCE_DIR}/tests/ui_capture.cpp"
    "${BLOCKPLANET_SOURCE_DIR}/src/ui/grid_texture.cpp"
    "${BLOCKPLANET_SOURCE_DIR}/src/ui/ui_grid.cpp"
    "${BLOCKPLANET_SOURCE_DIR}/src/ui/ui_navigator.cpp")
target_include_directories(ui_capture PRIVATE "${BLOCKPLANET_SOURCE_DIR}/src/ui" "${BLOCKPLANET_SOURCE_DIR}/include")
target_link_libraries(ui_capture PRIVATE RmlUi::RmlUi)
