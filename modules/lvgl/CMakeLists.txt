# Copyright (c) 2019 jan.van_winkel@dxplore.eu
# Copyright (c) 2020 Teslabs Engineering S.L.
# Copyright (c) 2023 Fabian Blatz <fabianblatz@gmail.com>
#
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_LVGL)

  set(ZEPHYR_CURRENT_LIBRARY lvgl)
  set(LVGL_DIR ${ZEPHYR_LVGL_MODULE_DIR})

  zephyr_interface_library_named(LVGL)
  zephyr_library()

  zephyr_include_directories(${LVGL_DIR}/src/)
  zephyr_include_directories(include)

  add_subdirectory_ifdef(CONFIG_LV_USE_NEMA_GFX nemagfx)

  zephyr_compile_definitions(LV_CONF_INCLUDE_SIMPLE=1)
  zephyr_library_compile_definitions(_POSIX_C_SOURCE=200809L)
  zephyr_compile_definitions(LV_CONF_PATH="${CMAKE_CURRENT_SOURCE_DIR}/include/lv_conf.h")

  include(sources.cmake)
  zephyr_library_sources(${LVGL_SRC_FILES} ${LVGL_GLUE_SOURCES})

  zephyr_library_link_libraries(LVGL)
  target_link_libraries(LVGL INTERFACE zephyr_interface)

endif()
