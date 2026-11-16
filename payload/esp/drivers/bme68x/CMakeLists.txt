idf_component_register(SRCS "bme68x.c" "bme68x_esp.c" "bme68x_spi_frame.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_driver_spi esp_timer)
