cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake"
        CACHE PATH "VitaSDK toolchain")
  else()
    message(FATAL_ERROR "VITASDK is not defined")
  endif()
endif()

project(VitaMaps VERSION 0.1.0 LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "VitaMaps")
set(VITA_TITLEID "VMAP00001")
set(VITA_VERSION "01.23")

set(VITAMAPS_HTTPS_PACKAGE
    "${CMAKE_CURRENT_SOURCE_DIR}/external/vita-https"
    CACHE PATH "Path to the vita-https source tree")
if(NOT EXISTS "${VITAMAPS_HTTPS_PACKAGE}/CMakeLists.txt")
  message(FATAL_ERROR
    "vita-https is missing. Clone with --recursive or run: "
    "git submodule update --init --recursive")
endif()

set(VITA_HTTPS_CURL_ROOT
    "${VITAMAPS_HTTPS_PACKAGE}/build/deps/curl-mbedtls"
    CACHE PATH "Pinned curl/Mbed TLS prefix produced by vita-https")
add_subdirectory("${VITAMAPS_HTTPS_PACKAGE}"
                 "${CMAKE_BINARY_DIR}/packages/vita-https")

# Release builds are explicitly -O3 throughout the application and transport.
# vita-https declares a conservative -O2 default; GCC honors the last level.
target_compile_options(vita_https PRIVATE -O3)

add_executable(${PROJECT_NAME}
  src/main.cpp
  src/app/app.cpp
  src/core/log.cpp
  src/cache/disk_cache.cpp
  src/cache/geocode_cache.cpp
  src/cache/memory_cache.cpp
  src/map/map_camera.cpp
  src/map/gpx.cpp
  src/map/mercator.cpp
  src/map/place_lookup.cpp
  src/map/pin_collection.cpp
  src/map/pin_geometry.cpp
  src/map/tile_manager.cpp
  src/map/tile_scheduler.cpp
  src/net/geocoder.cpp
  src/net/elevation.cpp
  src/net/map_http.cpp
  src/net/overpass.cpp
  src/providers/osm_provider.cpp
  src/render/image_decoder.cpp
  src/render/map_renderer.cpp
  src/render/texture_cache.cpp
  src/settings/preferences.cpp
  src/system/fatal_screen.cpp
  src/ui/font.c
  src/ui/localization.cpp
  src/ui/map_screen.cpp
  src/ui/motion.cpp
  src/ui/text_input.cpp
  "${VITASDK}/share/gcc-arm-vita-eabi/samples/common/debugScreen.c")

target_include_directories(${PROJECT_NAME} PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${VITASDK}/share/gcc-arm-vita-eabi/samples/common")
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)
target_compile_options(${PROJECT_NAME} PRIVATE
  -Wall -Wextra -Wpedantic
  -O3 -ffunction-sections -fdata-sections
  $<$<CONFIG:Release>:-DNDEBUG>
  $<$<CONFIG:Debug>:-g3>)
target_compile_definitions(${PROJECT_NAME} PRIVATE
  $<$<CONFIG:Debug>:VITAMAPS_DEBUG_BUILD=1>
  $<$<CONFIG:Debug>:VITAMAPS_BUILD_LABEL="Debug">
  $<$<CONFIG:Release>:VITAMAPS_BUILD_LABEL="Release">)
target_link_options(${PROJECT_NAME} PRIVATE
  $<$<CONFIG:Release>:-Wl,--gc-sections>)

target_link_libraries(${PROJECT_NAME} PRIVATE
  vita2d
  ScePgf_stub
  SceRegistryMgr_stub
  jpeg
  freetype
  bz2
  png
  z
  VitaHttps::VitaHttps
  SceCtrl_stub
  SceTouch_stub
  SceDisplay_stub
  SceGxm_stub
  SceAppMgr_stub
  SceCommonDialog_stub
  SceIme_stub
  SceSysmodule_stub
  SceNet_stub
  SceNetCtl_stub
  SceRtc_stub
  pthread
  m)

set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d ATTRIBUTE2=12")
vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE NOASLR)
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID}
  ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE sce_sys/icon0.png sce_sys/icon0.png
  FILE assets/fonts/Inter-Medium.ttf fonts/Inter-Medium.ttf
  FILE assets/fonts/Inter-SemiBold.ttf fonts/Inter-SemiBold.ttf
  FILE assets/fonts/Inter-OFL.txt licenses/Inter-OFL.txt
  FILE LICENSE licenses/VitaMaps-GPL-3.0.txt
  FILE THIRD_PARTY_NOTICES.md licenses/THIRD_PARTY_NOTICES.md)
add_dependencies(${PROJECT_NAME}.vpk-vpk ${PROJECT_NAME}.self-self)
