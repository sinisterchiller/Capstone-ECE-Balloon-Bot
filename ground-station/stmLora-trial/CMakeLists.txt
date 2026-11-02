# Set the minimum version of CMake that can be used
cmake_minimum_required(VERSION 3.20)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# ── Source-controlled build configuration ──────────────────────────────────
# When ON (Debug preset): role and TX come from Inc/app_build_config.h.
#   CMake does NOT define APP_ROLE_INITIATOR or APP_TX_ENABLED.
# When OFF (Release, InitiatorTx, ResponderTx): role and TX come from
#   CMake cache variables below, which are passed to the compiler.
option(APP_CONFIG_FROM_SOURCE "Use Inc/app_build_config.h for role/TX config" OFF)
set(APP_ROLE_INITIATOR 1 CACHE STRING "Board role: 1=Initiator, 0=Responder (used when APP_CONFIG_FROM_SOURCE=OFF)")
set(APP_TX_ENABLED 0 CACHE STRING "TX enable: 0=disabled (safe), 1=enabled (testing only) (used when APP_CONFIG_FROM_SOURCE=OFF)")

# Set target definition variables required by the toolchain file
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/target.cmake)

# Set the toolchain file for cross-compilation
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/gcc-arm-none-eabi.cmake)

# Project definition
project(stmLora-trial)

# Enable CMake support for ASM and C languages
enable_language(C ASM)

set(PROJECT_NAME stmLora-trial)

# Create an executable object type
add_executable(${PROJECT_NAME})

# Include additional CMake files for flags, components, and files
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/flags.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/components.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/files.cmake)

# Generate ELF + BIN + HEX from the ELF output
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMENT "Generating BIN and HEX files"
    COMMAND ${CMAKE_OBJCOPY} -O binary  "$<TARGET_FILE:${PROJECT_NAME}>"  "$<TARGET_FILE_DIR:${PROJECT_NAME}>/${PROJECT_NAME}.bin"
    COMMAND ${CMAKE_OBJCOPY} -O ihex   "$<TARGET_FILE:${PROJECT_NAME}>"  "$<TARGET_FILE_DIR:${PROJECT_NAME}>/${PROJECT_NAME}.hex"
    COMMAND ${CMAKE_SIZE}   "$<TARGET_FILE:${PROJECT_NAME}>"
    VERBATIM
)
