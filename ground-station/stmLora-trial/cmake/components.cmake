# cmake/components.cmake — single coherent source list for stmLora-trial
# All target_sources are defined here. flags.cmake handles includes/defines/linker.

# ── Linker script ──────────────────────────────────────────────────────────
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/stm32wl55xc_flash_cm4.ld"
               "${CMAKE_CURRENT_BINARY_DIR}/stm32wl55xc_flash_cm4.ld" COPYONLY)

set_target_properties(${PROJECT_NAME} PROPERTIES
    LINK_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/stm32wl55xc_flash_cm4.ld"
)

# ── CMSIS / device ────────────────────────────────────────────────────────
target_sources(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/startup_stm32wl55jcix.s"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/system_stm32wlxx.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/syscalls.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/sysmem.c"
)

# ── HAL driver ─────────────────────────────────────────────────────────────
target_sources(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_adc.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_adc_ex.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_comp.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_cortex.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_crc.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_crc_ex.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_cryp.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_cryp_ex.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_dac.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_dac_ex.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_dma.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_dma_ex.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_exti.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_flash.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_flash_ex.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_gpio.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_gtzc.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_hsem.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_i2c.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_i2c_ex.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_i2s.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_ipcc.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_irda.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_iwdg.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_lptim.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_pka.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_pwr.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_pwr_ex.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_rcc.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_rcc_ex.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_rng.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_rng_ex.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_rtc.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_rtc_ex.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_smartcard.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_smartcard_ex.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_smbus.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_smbus_ex.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_spi.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_spi_ex.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_subghz.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_tim.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_tim_ex.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_uart.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_uart_ex.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_usart.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_usart_ex.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_wwdg.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_ll_adc.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_ll_comp.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_ll_crc.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_ll_dac.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_ll_dma.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_ll_exti.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_ll_gpio.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_ll_i2c.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_ll_lptim.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_ll_lpuart.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_ll_pka.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_ll_pwr.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_ll_rcc.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_ll_rng.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_ll_rtc.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_ll_spi.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_ll_tim.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_ll_usart.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_ll_utils.c"
)

# ── BSP ────────────────────────────────────────────────────────────────────
target_sources(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/BSP/STM32WLxx_Nucleo/stm32wlxx_nucleo.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/BSP/STM32WLxx_Nucleo/stm32wlxx_nucleo_radio.c"
)

# ── SubGHz_Phy middleware (radio driver layer) ────────────────────────────
target_sources(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/SubGHz_Phy/radio_driver/radio.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/SubGHz_Phy/radio_driver/radio_driver.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/SubGHz_Phy/radio_driver/radio_fw.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/SubGHz_Phy/radio_driver/lr_fhss_mac.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/SubGHz_Phy/radio_driver/wl_lr_fhss.c"
)

# ── SubGHz_Phy application layer ──────────────────────────────────────────
target_sources(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/SubGHz_Phy/App/app_subghz_phy.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/SubGHz_Phy/App/subghz_phy_app.c"
)

# ── Board radio interface ─────────────────────────────────────────────────
target_sources(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/SubGHz_Phy/Target/radio_board_if.c"
)

# ── Utility libraries ─────────────────────────────────────────────────────
target_sources(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Utilities/sequencer/stm32_seq.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Utilities/trace/adv_trace/stm32_adv_trace.c"
    # Note: stm32_adv_trace_if_template.c removed — requires user-provided stm32_adv_trace_if.h
    "${CMAKE_CURRENT_SOURCE_DIR}/Utilities/timer/stm32_timer.c"
    # Note: stm32_timer_if_template.c removed — requires user-provided stm32_timer_if.h
)

# ── HAL MSP + interrupt handlers ──────────────────────────────────────────
target_sources(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/stm32wlxx_hal_msp.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/stm32wlxx_it.c"
)

# ── Peripheral init ───────────────────────────────────────────────────────
target_sources(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/dma.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/gpio.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/rtc.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/subghz.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/usart.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/timer_if.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/usart_if.c"
)

# ── System services ───────────────────────────────────────────────────────
target_sources(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/sys_app.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/sys_debug.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/stm32_lpm_if.c"
)

# ── Utilities (misc) ─────────────────────────────────────────────────────
target_sources(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Utilities/misc/stm32_mem.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Utilities/misc/stm32_tiny_vsnprintf.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Utilities/misc/stm32_tiny_sscanf.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Utilities/misc/stm32_systime.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Utilities/lpm/tiny_lpm/stm32_lpm.c"
)

# ── Main application ──────────────────────────────────────────────────────
target_sources(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/main.c"
)
