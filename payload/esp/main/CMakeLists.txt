idf_component_register(SRCS "main.cpp" "gps.cpp" "bme.cpp"
                       REQUIRES nmea_parser bme68x
                       INCLUDE_DIRS ".")
