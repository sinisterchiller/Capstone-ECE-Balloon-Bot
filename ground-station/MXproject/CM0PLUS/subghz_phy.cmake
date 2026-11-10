# -----------------------------------------------------------------------------
# SubGHz_Phy PingPong application running on CPU2 (Cortex-M0+)
#
# Sources and include paths are listed here (instead of in the CubeMX-generated
# mx-generated.cmake) so that regenerating the project from MXproject.ioc does
# not drop them. Included from CM0PLUS/CMakeLists.txt.
#
# Upstream provenance (all files below are STMicroelectronics sources, kept
# unmodified unless a CPU2 adaptation is noted in the file itself):
#   - Application/target layer:
#     STM32CubeWL v1.6.0
#     Projects/NUCLEO-WL55JC/Applications/SubGHz_Phy/SubGHz_Phy_PingPong
#     SubGHz_Phy/App/*, SubGHz_Phy/Target/*
#   - Radio middleware:
#     stm32-mw-subghz-phy v1.5.0 (Middlewares/Third_Party/SubGHz_Phy)
#     -> CM0PLUS/Middlewares/SubGHz_Phy/radio_driver
#   - Utilities:
#     STM32CubeWL v1.6.0 /Utilities/{misc,timer,sequencer,lpm/tiny_lpm,
#     trace/adv_trace} -> CM0PLUS/Utilities
#   - HAL modules (SUBGHZ, RTC) and the radio BSP:
#     stm32wlxx_hal_driver v1.6.0 and the STM32WLxx_Nucleo BSP version pinned by
#     CubeWL v1.6.0 (v1.3.1), added next to the existing CubeMX copies.
#
# License: see the LICENSE / LICENSE.md files in each imported directory and
# the file headers (SLA0044 - STMicroelectronics permissive license terms).
# -----------------------------------------------------------------------------

set(SUGHZ_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# --- HAL modules and BSP driver owned by CPU2 --------------------------------
target_sources(STM32_Drivers PRIVATE
    ${SUGHZ_ROOT}/../Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_subghz.c
    ${SUGHZ_ROOT}/../Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_rtc.c
    ${SUGHZ_ROOT}/../Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_rtc_ex.c
    ${SUGHZ_ROOT}/../Drivers/BSP/STM32WLxx_Nucleo/stm32wlxx_nucleo_radio.c
)

# --- Include paths -----------------------------------------------------------
target_include_directories(stm32cubemx INTERFACE
    ${SUGHZ_ROOT}/SubGHz_Phy/App
    ${SUGHZ_ROOT}/SubGHz_Phy/Target
    ${SUGHZ_ROOT}/Middlewares/SubGHz_Phy/radio_driver
    ${SUGHZ_ROOT}/Utilities/misc
    ${SUGHZ_ROOT}/Utilities/timer
    ${SUGHZ_ROOT}/Utilities/sequencer
    ${SUGHZ_ROOT}/Utilities/lpm/tiny_lpm
    ${SUGHZ_ROOT}/Utilities/trace/adv_trace
    ${SUGHZ_ROOT}/../Common/GnssVcp
)

# --- Application, target layer, middleware and utilities ---------------------
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # CubeMX-style peripheral/utility modules added for the radio application
    ${SUGHZ_ROOT}/Core/Src/gpio.c
    ${SUGHZ_ROOT}/Core/Src/usart.c
    ${SUGHZ_ROOT}/Core/Src/dma.c
    ${SUGHZ_ROOT}/Core/Src/rtc.c
    ${SUGHZ_ROOT}/Core/Src/subghz.c
    ${SUGHZ_ROOT}/Core/Src/sys_app.c
    ${SUGHZ_ROOT}/Core/Src/sys_debug.c
    ${SUGHZ_ROOT}/Core/Src/usart_if.c
    ${SUGHZ_ROOT}/Core/Src/timer_if.c
    ${SUGHZ_ROOT}/Core/Src/stm32_lpm_if.c
    # NMEA lines received by CPU1, printed on the ST-LINK VCP
    ${SUGHZ_ROOT}/Core/Src/gnss_vcp_trace.c
    # PingPong application
    ${SUGHZ_ROOT}/SubGHz_Phy/App/subghz_phy_app.c
    ${SUGHZ_ROOT}/SubGHz_Phy/App/app_subghz_phy.c
    ${SUGHZ_ROOT}/SubGHz_Phy/Target/radio_board_if.c
    # SubGHz_Phy radio middleware
    ${SUGHZ_ROOT}/Middlewares/SubGHz_Phy/radio_driver/radio.c
    ${SUGHZ_ROOT}/Middlewares/SubGHz_Phy/radio_driver/radio_driver.c
    ${SUGHZ_ROOT}/Middlewares/SubGHz_Phy/radio_driver/radio_fw.c
    ${SUGHZ_ROOT}/Middlewares/SubGHz_Phy/radio_driver/wl_lr_fhss.c
    ${SUGHZ_ROOT}/Middlewares/SubGHz_Phy/radio_driver/lr_fhss_mac.c
    # Utilities
    ${SUGHZ_ROOT}/Utilities/misc/stm32_mem.c
    ${SUGHZ_ROOT}/Utilities/misc/stm32_systime.c
    ${SUGHZ_ROOT}/Utilities/misc/stm32_tiny_vsnprintf.c
    ${SUGHZ_ROOT}/Utilities/misc/stm32_tiny_sscanf.c
    ${SUGHZ_ROOT}/Utilities/timer/stm32_timer.c
    ${SUGHZ_ROOT}/Utilities/sequencer/stm32_seq.c
    ${SUGHZ_ROOT}/Utilities/lpm/tiny_lpm/stm32_lpm.c
    ${SUGHZ_ROOT}/Utilities/trace/adv_trace/stm32_adv_trace.c
)
