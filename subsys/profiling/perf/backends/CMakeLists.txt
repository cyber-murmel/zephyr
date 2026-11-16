# Copyright (c) 2024 Meta Platforms
# Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

zephyr_sources_ifdef(CONFIG_PROFILING_PERF_BACKEND_RISCV
  perf_riscv.c
)

zephyr_sources_ifdef(CONFIG_PROFILING_PERF_BACKEND_ARM_CORTEX_M
  perf_arm_cortex_m.c
)

zephyr_sources_ifdef(CONFIG_PROFILING_PERF_BACKEND_X86
  perf_x86.c
)

zephyr_sources_ifdef(CONFIG_PROFILING_PERF_BACKEND_X86_64
  perf_x86_64.c
)

zephyr_sources_ifdef(CONFIG_PROFILING_PERF_BACKEND_ARM64
  perf_arm64.c
)

if(CONFIG_PROFILING_PERF_BACKEND_XTENSA)
  # The backtrace walker is internal to the architecture (xtensa_backtrace.h).
  zephyr_library()
  zephyr_library_sources(perf_xtensa.c)
  zephyr_library_include_directories(${ARCH_DIR}/${ARCH}/include)
endif()
