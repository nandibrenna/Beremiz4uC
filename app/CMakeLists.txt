#-------------------------------------------------------------------------------
# Zephyr Application
#
# Copyright (c) 2021 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(app LANGUAGES C CXX)

# erpc target code
set(ERPC_IDL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/erpc)
set(ERPC_IDL_FILE ${ERPC_IDL_DIR}/erpc_PLCObject.erpc)
set(ERPC_OUTPUT_DIR ${ERPC_IDL_DIR})
add_custom_command(
  OUTPUT "${ERPC_OUTPUT_DIR}/erpc_error_handler.cpp"
         "${ERPC_OUTPUT_DIR}/c_erpc_PLCObject_server.cpp"
		 "${ERPC_OUTPUT_DIR}/erpc_PLCObject_interface.cpp"
		 "${ERPC_OUTPUT_DIR}/erpc_PLCObject_server.cpp"
  COMMAND erpcgen -I "${ERPC_IDL_DIR}" -o "${ERPC_OUTPUT_DIR}" "${ERPC_IDL_FILE}"
  DEPENDS "${ERPC_IDL_FILE}"
  COMMENT "Generating eRPC code from ${ERPC_IDL_FILE}"
)

target_sources(app PRIVATE
				${ERPC_OUTPUT_DIR}/erpc_error_handler.cpp
				${ERPC_OUTPUT_DIR}/c_erpc_PLCObject_server.cpp
				${ERPC_OUTPUT_DIR}/erpc_PLCObject_interface.cpp
				${ERPC_OUTPUT_DIR}/erpc_PLCObject_server.cpp
				)
zephyr_include_directories(${ERPC_OUTPUT_DIR})

# udynlink
target_sources(app PRIVATE
				${CMAKE_CURRENT_SOURCE_DIR}/udynlink/udynlink.c
				)
zephyr_include_directories(
				${CMAKE_CURRENT_SOURCE_DIR}/udynlink
				)
target_compile_definitions(app PRIVATE UDYNLINK_SKIP_PROLOGUE)

# main app
FILE(GLOB app_sources ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c*)
target_sources(app PRIVATE ${app_sources})
zephyr_include_directories(include)