idf_component_register(SRCS "main.cpp" "gps.cpp" "bme.cpp" "mpu.cpp"
                       REQUIRES nmea_parser bme68x mpu6500
                       INCLUDE_DIRS ".")
