cmake_minimum_required(VERSION 3.16)
project(carplay_launcher_preview C CXX)
get_filename_component(SDK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
set(LV_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE STRING "" FORCE)
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL ON CACHE BOOL "" FORCE)
add_subdirectory("${SDK_ROOT}/packages/artinchip/lvgl-ui/lvgl_v9/lvgl" lvgl)
add_executable(launcher_preview main.c)
target_include_directories(launcher_preview PRIVATE
    ../support/lubanlite_demo ../../app ../../platform
    "${SDK_ROOT}/packages/third-party/rt-carplay/include"
    "${SDK_ROOT}/packages/third-party/rt-carplay/core/airplay/include")
target_compile_definitions(launcher_preview PRIVATE RTCARPLAY_INTERNET_SHARE RTCARPLAY_AUDIO RTCARPLAY_MICROPHONE
    RTCARPLAY_IAP2 LPKG_RT_CARPLAY_LUBANLITE_WIFI RTCP_AUDIO_CODECS RTCARPLAY_OTA)
target_compile_options(launcher_preview PRIVATE -Wall -Wextra -Werror)
target_link_libraries(launcher_preview PRIVATE lvgl m)

# Render the actual fusion dashboard without a panel, decoder or phone.
add_executable(fusion_preview fusion_main.c ../../app/fusion_display.c
    "${SDK_ROOT}/packages/third-party/rt-carplay/core/media/display.c")
target_include_directories(fusion_preview PRIVATE ../support/lubanlite_video ../../app ../../platform
    "${SDK_ROOT}/packages/third-party/rt-carplay/include")
target_compile_definitions(fusion_preview PRIVATE RTCARPLAY_SINGLE_SCREEN_FUSION)
target_compile_options(fusion_preview PRIVATE -Wall -Wextra -Werror)
target_link_libraries(fusion_preview PRIVATE lvgl m)
