# SPDX-FileCopyrightText: 2022-2025 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: CC0-1.0
"""
Tests for the GPS demo application.

These tests verify that the ESP32 GPS demo application boots correctly and
logs the expected no-fix state on startup. A valid GPS fix requires
physical hardware (u-blox NEO-6 receiver) and outdoor conditions; it is
not achievable in QEMU or CI environments without a simulated GPS source.

Test limitations:
- pytest-embedded with QEMU cannot provide NMEA data to the UART RX pin.
- The NMEA parser component reads from hardware UART; no software stub is
  injected in these tests.
- Therefore, tests only verify boot and initial no-fix log output.
- Hardware integration tests (valid fix, disconnect/reconnect) require
  physical ESP32 + GPS receiver and are out of scope for automated CI.
- The Linux host target is unsupported because the GPS driver uses
  ESP32 hardware UART (esp_driver_uart); no Linux port exists.
"""
import pytest
from pytest_embedded_idf.dut import IdfDut
from pytest_embedded_idf.utils import idf_parametrize


@pytest.mark.generic
@idf_parametrize('target', ['esp32'], indirect=['target'])
def test_gps_demo_boot_and_no_fix(dut: IdfDut) -> None:
    """Verify GPS demo boots and reports initial no-fix state."""
    # App should start and initialize GPS
    dut.expect('GPS demo started, waiting for updates...', timeout=10)
    # With no GPS hardware connected, the adapter should report no valid fix
    dut.expect('No valid fix (no GPS data received yet or fix stale)', timeout=15)


# QEMU tests are not meaningful for GPS demo because QEMU cannot simulate
# a GPS receiver feeding NMEA sentences to the UART. The test would only
# see the no-fix message, which is already covered by the generic test above.
# If a QEMU-compatible UART NMEA stub becomes available, add a test here.
# Linux host target is unsupported (no esp_driver_uart on Linux).