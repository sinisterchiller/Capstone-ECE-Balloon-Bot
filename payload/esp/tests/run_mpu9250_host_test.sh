#!/bin/sh
#
# Build and run the archival MPU-9250 host regression test with the host C
# compiler. No ESP-IDF, no hardware, no Python and no package manager are
# required.
#
# Usage: payload/esp/tests/run_mpu9250_host_test.sh
#
# This suite is historical. The payload no longer runs an MPU-9250: main/mpu.cpp
# forwards to drivers/mpu6500, so the active integrations are tested by
# tests/run_mpu6500_host_test.sh. What remains here is the record of the vendored
# MPU-9250 driver's behavior, kept runnable so the previous part still has a
# reference:
#
#   tests/mpu9250_host_test.c against the unmodified upstream driver
#   (drivers/mpu9250/src/driver_mpu9250.c) with mocked I2C callbacks - address
#   selection, MPU and AK8963 init, nine-axis scaling, wrong sensor IDs and
#   transport failures.
#
# It is deliberately vendor-only: nothing here is linked against main/mpu.cpp or
# the mpu6500_esp_* seam, because the application layer no longer talks to the
# MPU-9250 and a shared binary would only hide that.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
driver=$(dirname "$here")/drivers/mpu9250
cc=${CC:-cc}

cflags="-std=c11 -Wall -Wextra -Werror -I $driver/src"

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
