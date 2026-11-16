# Copyright (c) 2019 jan.van_winkel@dxplore.eu
# Copyright (c) 2020 Teslabs Engineering S.L.
# Copyright (c) 2023 Fabian Blatz <fabianblatz@gmail.com>
#
# SPDX-License-Identifier: Apache-2.0
#
# LVGL's sources and Zephyr's glue, apart from how they are built: for this directory's library, and
# for an application that builds LVGL its own way. Needs LVGL_DIR; sets LVGL_SRC_FILES and
# LVGL_GLUE_SOURCES.

file(GLOB_RECURSE LVGL_SRC_FILES CONFIGURE_DEPENDS
  ${LVGL_DIR}/src/*.c
)

set(LVGL_EXCLUDE_PATTERNS
    "${LVGL_DIR}/src/osal/lv_cmsis_rtos2.c"
    "${LVGL_DIR}/src/osal/lv_freertos.c"
    "${LVGL_DIR}/src/osal/lv_linux.c"
    "${LVGL_DIR}/src/osal/lv_mqx.c"
    "${LVGL_DIR}/src/osal/lv_os_none.c"
    "${LVGL_DIR}/src/osal/lv_pthread.c"
    "${LVGL_DIR}/src/osal/lv_rtthread.c"
    "${LVGL_DIR}/src/osal/lv_sdl2.c"
    "${LVGL_DIR}/src/osal/lv_windows.c"

    "${LVGL_DIR}/src/drivers/*"

    # Remove libs/gltf since this leads to build warnings
    "${LVGL_DIR}/src/libs/gltf/*"

    "${LVGL_DIR}/src/stdlib/builtin/lv_mem_core_builtin.c"
    "${LVGL_DIR}/src/stdlib/builtin/lv_sprintf_builtin.c"
    "${LVGL_DIR}/src/stdlib/builtin/lv_string_builtin.c"
    "${LVGL_DIR}/src/stdlib/clib/lv_mem_core_clib.c"

    "${LVGL_DIR}/src/stdlib/rtthread/*"
    "${LVGL_DIR}/src/stdlib/micropython/*"
    "${LVGL_DIR}/src/stdlib/uefi/*"

    "${LVGL_DIR}/src/debugging/test/*"
)

foreach(pattern IN LISTS LVGL_EXCLUDE_PATTERNS)
  file(GLOB_RECURSE LVGL_EXCLUDED CONFIGURE_DEPENDS ${pattern})
  list(REMOVE_ITEM LVGL_SRC_FILES ${LVGL_EXCLUDED})
endforeach()

set(LVGL_GLUE_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/lvgl.c
    ${CMAKE_CURRENT_LIST_DIR}/lvgl_display.c
    ${CMAKE_CURRENT_LIST_DIR}/lvgl_display_8bit.c
    ${CMAKE_CURRENT_LIST_DIR}/lvgl_display_16bit.c
    ${CMAKE_CURRENT_LIST_DIR}/lvgl_display_24bit.c
    ${CMAKE_CURRENT_LIST_DIR}/lvgl_display_32bit.c
    ${CMAKE_CURRENT_LIST_DIR}/lvgl_zephyr_osal.c
)
foreach(glue
        LV_Z_MONOCHROME_CONVERSION_BUFFER:lvgl_display_mono.c
        LV_Z_USE_FILESYSTEM:lvgl_fs.c
        LV_Z_MEM_POOL_SYS_HEAP:lvgl_mem.c
        LV_Z_SHELL:lvgl_shell.c
        LVGL:input/lvgl_common_input.c
        LV_Z_POINTER_INPUT:input/lvgl_pointer_input.c
        LV_Z_BUTTON_INPUT:input/lvgl_button_input.c
        LV_Z_ENCODER_INPUT:input/lvgl_encoder_input.c
        LV_Z_KEYPAD_INPUT:input/lvgl_keypad_input.c)
  string(REPLACE ":" ";" glue "${glue}")
  list(GET glue 0 option)
  list(GET glue 1 source)
  if(CONFIG_${option})
    list(APPEND LVGL_GLUE_SOURCES ${CMAKE_CURRENT_LIST_DIR}/${source})
  endif()
endforeach()
