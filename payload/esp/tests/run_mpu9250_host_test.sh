#!/bin/sh
#
# Build and run the MPU-9250 host regression tests with the host compilers.
# No ESP-IDF, no hardware, no Python and no package manager are required.
#
# Usage: payload/esp/tests/run_mpu9250_host_test.sh
#
# Two binaries are built and run:
#
#   driver  - tests/mpu9250_host_test.c against the unmodified upstream driver
#             (drivers/mpu9250/src/driver_mpu9250.c) with mocked I2C callbacks:
#             address selection, MPU and AK8963 init, nine-axis scaling, wrong
#             sensor IDs and transport failures.
#
#   wrapper - tests/mpu_wrapper_host_test.cpp against main/mpu.cpp with a fake
#             mpu9250_esp_* adapter and tests/stubs/esp_err.h standing in for the
#             ESP-IDF headers: Mpu lifecycle and error forwarding.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
esp=$(dirname "$here")
driver="$esp/drivers/mpu9250"
cc=${CC:-cc}
cxx=${CXX:-c++}

cflags="-std=c11 -Wall -Wextra -Werror -I $driver/src"
cxxflags="-std=c++17 -Wall -Wextra -Werror -I $esp/main -I $driver -I $here/stubs"

build_and_run() {
    name=$1
    compiler=$2
    flags=$3
    shift 3

    out="${TMPDIR:-/tmp}/mpu9250_host_test_${name}"

    # shellcheck disable=SC2086 # word splitting of the flag list is intended
    "$compiler" $flags "$@" -lm -o "$out"
    "$out"
}

build_and_run driver "$cc" "$cflags" \
    "$here/mpu9250_host_test.c" "$driver/src/driver_mpu9250.c"

build_and_run wrapper "$cxx" "$cxxflags" \
    "$here/mpu_wrapper_host_test.cpp" "$esp/main/mpu.cpp"
