set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".elf")

# ── Compile options ────────────────────────────────────────────────────────
target_compile_options(${PROJECT_NAME} PUBLIC
    ${CPU_FLAGS}
    ${CC_SECURE}
    $<$<CONFIG:Debug>:
      $<$<COMPILE_LANGUAGE:C>:
        -std=gnu11
        -fstack-usage
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
        -O0
        -g3
        -ggdb
      >
      $<$<COMPILE_LANGUAGE:CXX>:
        -std=gnu++11
        -fstack-usage
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
        -O0
        -g3
        -ggdb
      >
      $<$<COMPILE_LANGUAGE:ASM>:
        -x
        assembler-with-cpp
        -MMD
        -MP
        -g
      >
    >
    $<$<CONFIG:Release>:
      $<$<COMPILE_LANGUAGE:C>:
        -std=gnu11
        -fstack-usage
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
        -Os
      >
      $<$<COMPILE_LANGUAGE:CXX>:
        -std=gnu++11
        -fstack-usage
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
        -Os
      >
      $<$<COMPILE_LANGUAGE:ASM>:
        -x
        assembler-with-cpp
        -MMD
        -MP
      >
    >
)

# ── Include directories ────────────────────────────────────────────────────
target_include_directories(${PROJECT_NAME} PRIVATE
    # User headers
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc"
    # CMSIS core (ARM CMSIS 6)
    "${CMAKE_CURRENT_SOURCE_DIR}/CMSIS_6/CMSIS/Core/Include"
    # CMSIS device (ST STM32WLxx — copied from cmsis-device-wl)
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32WLxx/Include"
    # HAL driver
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Inc"
    # BSP
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/BSP/STM32WLxx_Nucleo"
    # SubGHz_Phy middleware (radio driver + target config)
    "${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/SubGHz_Phy"
    "${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/SubGHz_Phy/radio_driver"
    "${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/SubGHz_Phy/Target"
    # SubGHz_Phy application layer (our adapted copy)
    "${CMAKE_CURRENT_SOURCE_DIR}/SubGHz_Phy/App"
    "${CMAKE_CURRENT_SOURCE_DIR}/SubGHz_Phy/Target"
    # Utility libraries
    "${CMAKE_CURRENT_SOURCE_DIR}/Utilities/sequencer"
    "${CMAKE_CURRENT_SOURCE_DIR}/Utilities/trace/adv_trace"
    "${CMAKE_CURRENT_SOURCE_DIR}/Utilities/timer"
    "${CMAKE_CURRENT_SOURCE_DIR}/Utilities/lpm/tiny_lpm"
)

# ── Compile definitions (C-only) ──────────────────────────────────────────
target_compile_definitions(${PROJECT_NAME} PRIVATE
    # CMSIS device definition (all languages)
    STM32WL55xx

    # HAL peripheral defines (all languages)
    USE_HAL_DRIVER

    # Board definition (all languages)
    TARGET_NUCLEO_WL55JC2

    # C-only defines
    $<$<COMPILE_LANGUAGE:C>:
        # Debug symbols
        $<$<CONFIG:Debug>:
            RTE_DEVICE_STARTUP_STM32WLXX
            DEBUG
        >
    >
)

# ── Role & TX configuration (configure-time, not generator-expression) ──────
#
#   APP_CONFIG_FROM_SOURCE=ON  → CMake does NOT define APP_ROLE_INITIATOR or
#                                  APP_TX_ENABLED; the shared header
#                                  (Inc/app_build_config.h) provides the values.
#   APP_CONFIG_FROM_SOURCE=OFF (default) → CMake provides defaults via
#                                  if(NOT DEFINED ...), allowing user -D flags
#                                  to override.  Explicit presets
#                                  (InitiatorTx, ResponderTx, Release) use OFF.

if(NOT APP_CONFIG_FROM_SOURCE)
  if(NOT DEFINED APP_ROLE_INITIATOR)
    set(APP_ROLE_INITIATOR 1 CACHE STRING "1=Initiator, 0=Responder")
  endif()
  if(NOT DEFINED APP_TX_ENABLED)
    set(APP_TX_ENABLED 0 CACHE STRING "1=TX enabled, 0=TX disabled")
  endif()
  set(_ROLE_TX_DEFS
    APP_ROLE_INITIATOR=${APP_ROLE_INITIATOR}
    APP_TX_ENABLED=${APP_TX_ENABLED}
  )
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE
    ${_ROLE_TX_DEFS}
)

# ── Linker options (C-only, scoped to Debug/Release) ──────────────────────
#
# Do NOT hardcode -L paths into a specific GCC installation or version.
# Let the selected arm-none-eabi-gcc driver resolve its own sysroot and
# library directories (libc, libgcc, libstdc++).  Hard-coding paths from
# one toolchain version while using a different compiler causes relocation
# failures ('Unknown destination type (ARM/Thumb)', 'dangerous relocation:
# unsupported relocation').
#
# The -mcpu / -mfloat-abi / -mthumb flags are set by the toolchain file
# (cmake/gcc-arm-none-eabi.cmake) via CPU_FLAGS and are passed to the
# linker through the driver so it selects the correct multilib.

target_link_options(${PROJECT_NAME} PRIVATE
    # CPU / Thumb / float-ABI — drives correct multilib selection in linker
    -mcpu=cortex-m4
    -mfloat-abi=soft
    -mthumb
    # Suppress dangerous relocation warnings (binutils 2.42+ compatibility)
    -Wl,--no-warn-rwx-segments
    $<$<CONFIG:Debug>:
        -T${CMAKE_CURRENT_BINARY_DIR}/stm32wl55xc_flash_cm4.ld
        -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/stmLora-trial.map
        -u
        _printf_float
        --specs=nosys.specs
        -Wl,--start-group
        -lc
        -lm
        -lstdc++
        -lsupc++
        -Wl,--end-group
        -Wl,-z,max-page-size=8
        -Wl,--print-memory-usage
    >

    $<$<CONFIG:Release>:
        -T${CMAKE_CURRENT_BINARY_DIR}/stm32wl55xc_flash_cm4.ld
        -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/stmLora-trial.map
        -u
        _printf_float
        --specs=nosys.specs
        -Wl,--start-group
        -lc
        -lm
        -lstdc++
        -lsupc++
        -Wl,--end-group
        -Wl,-z,max-page-size=8
        -Wl,--print-memory-usage
    >
)

# Clean generated files
set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_CLEAN_FILES
    "${CMAKE_SOURCE_DIR}/build/${CMAKE_BUILD_TYPE}/stm32wl55xc_flash_cm4.ld"
    "${CMAKE_SOURCE_DIR}/build/${CMAKE_BUILD_TYPE}/stmLora-trial.map"
    "${CMAKE_SOURCE_DIR}/build/${CMAKE_BUILD_TYPE}/stmLora-trial.elf"
    "${CMAKE_SOURCE_DIR}/build/${CMAKE_BUILD_TYPE}/stmLora-trial.bin"
    "${CMAKE_SOURCE_DIR}/build/${CMAKE_BUILD_TYPE}/stmLora-trial.hex"
)
