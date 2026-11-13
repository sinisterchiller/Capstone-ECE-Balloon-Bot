idf_component_register(SRCS "src/driver_mpu6500.c" "mpu6500_esp.c"
                       INCLUDE_DIRS "." "src"
                       PRIV_REQUIRES esp_driver_i2c esp_timer)
