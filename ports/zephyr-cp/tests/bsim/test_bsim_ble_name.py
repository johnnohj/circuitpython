# SPDX-FileCopyrightText: 2026 Scott Shawcroft for Adafruit Industries
# SPDX-License-Identifier: MIT

"""BLE name tests for nrf5340bsim."""

import pytest

pytestmark = pytest.mark.circuitpython_board("native_nrf5340bsim")

BSIM_NAME_CODE = """\
import _bleio

adapter = _bleio.adapter
adapter.enabled = True
adapter.name = "CPNAME"
print("name", adapter.name)
"""


@pytest.mark.circuitpy_drive({"code.py": BSIM_NAME_CODE})
def test_bsim_set_name(bsim_phy, circuitpython):
    """Set the BLE name and read it back on bsim."""
    circuitpython.wait_until_done()

    assert "name CPNAME" in circuitpython.serial.all_output
