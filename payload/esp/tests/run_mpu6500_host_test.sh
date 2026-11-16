#!/bin/sh
#
# Build and run the MPU-6500 host regression tests with the host compilers.
# No ESP-IDF, no hardware, no Python and no package manager are required.
#
# Usage: payload/esp/tests/run_mpu6500_host_test.sh
#
# Three binaries are built and run, covering the layers of the active
# integration (main/mpu.cpp -> drivers/mpu6500/mpu6500_esp.c -> vendored
# LibDriver driver):
#
#   driver  - tests/mpu6500_host_test.c against the unmodified upstream driver
#             (drivers/mpu6500/src/driver_mpu6500.c) driven through the LibDriver
#             callback contract: I2C address selection, WHO_AM_I 0x70 accepted
#             and 0x71 rejected, transport/reset failures, the adapter's
#             configuration, six-axis scaling for every configured range.
#
#   adapter - tests/mpu6500_esp_adapter_host_test.c against the unmodified
#             adapter (drivers/mpu6500/mpu6500_esp.c) and the same upstream
#             driver, with the mock ESP-IDF I2C master, esp_timer, FreeRTOS delay
#             and logging in tests/mpu6500_esp_mock_idf.c. This is the ESP-facing
#             side: ESP error codes for 0x70 / 0x71 / I2C failures, the six-axis
#             reading and its timestamp, and power down.
#
#   wrapper - tests/mpu6500_wrapper_host_test.cpp against main/mpu.cpp with a fake
#             mpu6500_esp_* adapter and tests/stubs/esp_err.h standing in for the
#             ESP-IDF header: Mpu lifecycle and error forwarding.
#
# The historical MPU-9250 tests are separate: tests/run_mpu9250_host_test.sh.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
esp=$(dirname "$here")
driver="$esp/drivers/mpu6500"
cc=${CC:-cc}
cxx=${CXX:-c++}

cflags="-std=c11 -Wall -Wextra -Werror -I $here -I $driver/src"
adapter_cflags="$cflags -I $here/stubs -I $here/stubs/idf -I $driver"
cxxflags="-std=c++17 -Wall -Wextra -Werror -I $esp/main -I $driver -I $here/stubs"

build_and_run() {
    name=$1
    compiler=$2
    flags=$3
    shift 3

    out="${TMPDIR:-/tmp}/mpu6500_host_test_${name}"

    # shellcheck disable=SC2086 # word splitting of the flag list is intended
    "$compiler" $flags "$@" -lm -o "$out"
    "$out"
}

build_and_run driver "$cc" "$cflags" \
    "$here/mpu6500_host_test.c" "$driver/src/driver_mpu6500.c"

build_and_run adapter "$cc" "$adapter_cflags" \
    "$here/mpu6500_esp_adapter_host_test.c" "$here/mpu6500_esp_mock_idf.c" \
    "$driver/mpu6500_esp.c" "$driver/src/driver_mpu6500.c"

build_and_run wrapper "$cxx" "$cxxflags" \
    "$here/mpu6500_wrapper_host_test.cpp" "$esp/main/mpu.cpp"
