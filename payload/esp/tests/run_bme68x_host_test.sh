#!/bin/sh
#
# Build and run the BME68x host regression test with the host C compiler.
# No ESP-IDF, no hardware and no Python are required.
#
# Usage: payload/esp/tests/run_bme68x_host_test.sh
#
# The test is built twice: with the default float compensation (BME68X_USE_FPU,
# the configuration bme68x_esp.c normally sees) and with
# -DBME68X_DO_NOT_USE_FPU, which covers the adapter's scaled-unit branch.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
driver=$(dirname "$here")/drivers/bme68x
cc=${CC:-cc}

sources="$here/bme68x_host_test.c $driver/bme68x.c $driver/bme68x_spi_frame.c"
cflags="-std=c11 -Wall -Wextra -Werror -I $driver"

build_and_run() {
    name=$1
    shift
    out="${TMPDIR:-/tmp}/bme68x_host_test_${name}"

    # shellcheck disable=SC2086 # word splitting of the flag list is intended
    $cc $cflags "$@" $sources -lm -o "$out"
    "$out"
}

build_and_run float
build_and_run integer -DBME68X_DO_NOT_USE_FPU
