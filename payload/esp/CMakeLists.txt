# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Register extra component directories before including project.cmake
# so that components in drivers/ are discoverable as regular components.
list(APPEND EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/drivers)

# With MINIMAL_BUILD ON, main declares its dependency on nmea_parser
# in its own CMakeLists.txt, so no COMPONENTS override is needed.

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
project(esp)