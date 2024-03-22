# SPDX-License-Identifier: Apache-2.0

board_runner_args(jlink "--device=STM32F407VE" "--speed=4000" "--reset-after-load")

include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
include(${ZEPHYR_BASE}/boards/common/blackmagicprobe.board.cmake)
include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)include(${ZEPHYR_BASE}/boards/common/blackmagicprobe.board.cmake)
