idf_component_register(SRCS "src/driver_mpu9250.c" "mpu9250_esp.c"
                       INCLUDE_DIRS "." "src"
                       PRIV_REQUIRES esp_driver_i2c esp_timer)
