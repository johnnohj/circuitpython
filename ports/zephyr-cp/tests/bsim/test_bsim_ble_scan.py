# SPDX-FileCopyrightText: 2025 Scott Shawcroft for Adafruit Industries
# SPDX-License-Identifier: MIT

"""BLE scanning tests for nrf5340bsim."""

import pytest

pytestmark = pytest.mark.circuitpython_board("native_nrf5340bsim")

BSIM_SCAN_CODE = """\
import _bleio

adapter = _bleio.adapter
print("scan start")
scan = adapter.start_scan(timeout=4.0, active=True)
found = False
for entry in scan:
    if b"zephyrproject" in entry.advertisement_bytes:
        print("found beacon")
        found = True
        break
adapter.stop_scan()
print("scan done", found)
"""

BSIM_SCAN_RELOAD_CODE = """\
import _bleio
import time

adapter = _bleio.adapter

print("scan run start")
found = False
for entry in adapter.start_scan(active=True):
    if b"zephyrproject" in entry.advertisement_bytes:
        print("found beacon run")
        found = True
        break
adapter.stop_scan()
print("scan run done", found)
"""

BSIM_SCAN_RELOAD_NO_STOP_CODE = """\
import _bleio
import time

adapter = _bleio.adapter

print("scan run start")
found = False
for entry in adapter.start_scan(active=True):
    if b"zephyrproject" in entry.advertisement_bytes:
        print("found beacon run")
        found = True
        break
print("scan run done", found)
"""


@pytest.mark.zephyr_sample("bluetooth/beacon")
@pytest.mark.circuitpy_drive({"code.py": BSIM_SCAN_CODE})
def test_bsim_scan_zephyr_beacon(bsim_phy, circuitpython, zephyr_sample):
    """Scan for Zephyr beacon sample advertisement using bsim."""
    _ = zephyr_sample

    circuitpython.wait_until_done()

    output = circuitpython.serial.all_output
    assert "scan start" in output
    assert "found beacon" in output
    assert "scan done True" in output


@pytest.mark.zephyr_sample("bluetooth/beacon")
@pytest.mark.code_py_runs(2)
@pytest.mark.duration(4)
@pytest.mark.circuitpy_drive({"code.py": BSIM_SCAN_RELOAD_CODE})
def test_bsim_scan_zephyr_beacon_reload(bsim_phy, circuitpython, zephyr_sample):
    """Scan for Zephyr beacon, soft reload, and scan again."""
    _ = zephyr_sample

    circuitpython.serial.wait_for("scan run done")
    circuitpython.serial.wait_for("Press any key to enter the REPL")
    circuitpython.serial.write("\x04")

    circuitpython.wait_until_done()

    output = circuitpython.serial.all_output
    assert output.count("scan run start") >= 2
    assert output.count("found beacon run") >= 2
    assert output.count("scan run done True") >= 2


@pytest.mark.flaky(reruns=3)
@pytest.mark.zephyr_sample("bluetooth/beacon")
@pytest.mark.code_py_runs(2)
@pytest.mark.duration(8)
@pytest.mark.circuitpy_drive({"code.py": BSIM_SCAN_RELOAD_NO_STOP_CODE})
def test_bsim_scan_zephyr_beacon_reload_no_stop(bsim_phy, circuitpython, zephyr_sample):
    """Scan for Zephyr beacon without explicit stop, soft reload, and scan again."""
    _ = zephyr_sample

    circuitpython.serial.wait_for("scan run done")
    circuitpython.serial.wait_for("Press any key to enter the REPL")
    circuitpython.serial.write("\x04")

    circuitpython.wait_until_done()

    output = circuitpython.serial.all_output
    assert output.count("scan run start") >= 2
    assert output.count("found beacon run") >= 2
    assert output.count("scan run done True") >= 2
