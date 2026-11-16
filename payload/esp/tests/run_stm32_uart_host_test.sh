#!/bin/sh
#
# Build and run the STM32 UART protocol host regression test with the host C compiler.
# No ESP-IDF, no hardware and no Python are required.
#
# Usage: payload/esp/tests/run_stm32_uart_host_test.sh
#
# The test covers the framing/parser edge cases for the STM32 UART link:
#   - split line across reads
#   - two frames per read
#   - CRLF handling
#   - empty lines
#   - overlong lines
#   - malformed input (invalid bytes)
#   - queue full behavior
#   - recovery after overflow

set -eu

here=$(cd "$(dirname "$0")" && pwd)
cc=${CC:-cc}

sources="$here/stm32_uart_protocol_host_test.c $here/stm32_uart_protocol.c"
cflags="-std=c11 -Wall -Wextra -Werror -I $here"

out="${TMPDIR:-/tmp}/stm32_uart_protocol_host_test"

# shellcheck disable=SC2086 # word splitting of the flag list is intended
$cc $cflags $sources -lm -o "$out"
"$out"