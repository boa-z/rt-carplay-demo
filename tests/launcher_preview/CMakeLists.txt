cmake_minimum_required(VERSION 3.16)
project(carplay_launcher_preview C CXX)
get_filename_component(SDK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
set(LV_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE STRING "" FORCE)
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL ON CACHE BOOL "" FORCE)
add_subdirectory("${SDK_ROOT}/packages/artinchip/lvgl-ui/lvgl_v9/lvgl" lvgl)

set(UI_SOURCES
    ../../ui/ui_language.c
    ../../ui/carplay_ui_theme.c
    ../../ui/icons/carplay_icons.c
    ../../ui/fonts/carplay_display_220.c
    ../../ui/fonts/carplay_display_110.c
    ../../ui/fonts/carplay_display_115.c
    ../../ui/fonts/carplay_display_58.c
    ../../ui/fonts/carplay_ui_34.c
    ../../ui/fonts/carplay_ui_21.c
    ../../ui/fonts/carplay_ui_18.c
    ../../ui/fonts/carplay_ui_17.c
    ../../ui/fonts/carplay_ui_15.c
    ../../ui/fonts/carplay_ui_14.c
    ../../ui/fonts/carplay_cjk_16.c
    ../../ui/fonts/carplay_cjk_18.c
    ../../ui/fonts/carplay_cjk_24.c
    ../../ui/fonts/carplay_cjk_30.c)

# Actual V4 pages with modeled connection, audio, backlight and OTA owners.
add_executable(launcher_preview main.c)
target_sources(launcher_preview PRIVATE ${UI_SOURCES} ../../ui/fonts/carplay_cjk_full_18.c ../../app/gnss_demo.c)
target_include_directories(launcher_preview PRIVATE
    ../../ui ../support/lubanlite_demo ../../app ../../platform
    "${SDK_ROOT}/packages/third-party/rt-carplay/include"
    "${SDK_ROOT}/packages/third-party/rt-carplay/core/airplay/include")
target_compile_definitions(launcher_preview PRIVATE
    RTCARPLAY_INTERNET_SHARE RTCARPLAY_MICROPHONE RTCARPLAY_IAP2
    LPKG_RT_CARPLAY_LUBANLITE_WIFI RTCARPLAY_OTA
    CARPLAY_PREFERENCES_EXTERNAL_PROVIDER)
target_compile_options(launcher_preview PRIVATE -Wall -Wextra -Werror)
target_link_libraries(launcher_preview PRIVATE lvgl m)

# The FUSION instrument strip rendered with the real fusion owner.
add_executable(fusion_preview fusion_main.c ../../app/fusion_display.c
    "${SDK_ROOT}/packages/third-party/rt-carplay/core/media/display.c")
target_sources(fusion_preview PRIVATE ${UI_SOURCES})
target_include_directories(fusion_preview PRIVATE ../../ui
    ../support/lubanlite_video ../../app ../../platform
    "${SDK_ROOT}/packages/third-party/rt-carplay/include")
target_compile_definitions(fusion_preview PRIVATE RTCARPLAY_SINGLE_SCREEN_FUSION
    CARPLAY_PREFERENCES_EXTERNAL_PROVIDER)
target_compile_options(fusion_preview PRIVATE -Wall -Wextra -Werror)
target_link_libraries(fusion_preview PRIVATE lvgl m)
