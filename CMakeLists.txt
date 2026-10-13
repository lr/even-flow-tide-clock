cmake_minimum_required(VERSION 3.16)
project(even_flow_clock C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()
find_package(SDL2 REQUIRED CONFIG)

set(WARNINGS -Wall -Wextra -Wshadow)

# --- LVGL (pinned submodule in lib/lvgl, configured by config/lv_conf.h) ---
set(LV_BUILD_CONF_DIR ${CMAKE_CURRENT_SOURCE_DIR}/config CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory(lib/lvgl)
target_link_libraries(lvgl PUBLIC SDL2::SDL2)

# --- Portable code: everything under src/ must also build for the ESP32 ---

# Astronomy Engine v2.1.19 (vendored, MIT): sun and moon positions
add_library(astronomy STATIC lib/astronomy/astronomy.c)
target_include_directories(astronomy PUBLIC lib/astronomy)
target_link_libraries(astronomy PUBLIC m)

# cJSON v1.7.19 (vendored, MIT): parsing web API responses
add_library(cjson STATIC lib/cjson/cJSON.c)
target_include_directories(cjson PUBLIC lib/cjson)

# Settings and data: plain C, no LVGL
add_library(even_flow_core STATIC
    src/config/config.c
    src/data/current.c
    src/data/curve.c
    src/data/noaa.c
    src/data/sky.c
    src/data/tide.c
    src/data/weather.c
)
target_include_directories(even_flow_core PUBLIC src)
target_link_libraries(even_flow_core PUBLIC astronomy cjson m)
target_compile_options(even_flow_core PRIVATE ${WARNINGS})

# The dial
add_library(even_flow_ui STATIC
    src/ui/dial.c
    src/ui/setup_card.c
    src/ui/weather_icons.c
    src/ui/icons/meteocons.c
)
target_link_libraries(even_flow_ui PUBLIC even_flow_core lvgl)
target_compile_options(even_flow_ui PRIVATE ${WARNINGS})

# --- Desktop simulator ---
add_executable(even_flow_sim sim/main.c)
target_link_libraries(even_flow_sim PRIVATE even_flow_ui)
target_compile_definitions(even_flow_sim PRIVATE EVEN_FLOW_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(even_flow_sim PRIVATE ${WARNINGS})

# --- Unit tests (core only, no display) ---
add_executable(even_flow_tests tests/test_main.c)
target_link_libraries(even_flow_tests PRIVATE even_flow_core)
target_compile_definitions(even_flow_tests PRIVATE EVEN_FLOW_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(even_flow_tests PRIVATE ${WARNINGS})
add_test(NAME core COMMAND even_flow_tests)

# --- Settings checker for tools/dev_server.py: the board's validation, on the Mac ---
add_executable(even_flow_config_check tools/config_check.c)
target_link_libraries(even_flow_config_check PRIVATE even_flow_core)
target_compile_options(even_flow_config_check PRIVATE ${WARNINGS})
