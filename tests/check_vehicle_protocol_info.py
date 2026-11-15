#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Independently decode /info built from the actual SIMULATED engine model."""
import plistlib
import sys
from pathlib import Path

info = plistlib.loads(Path(sys.argv[1]).read_bytes())
vehicle = info["vehicleStateProtocolInfo"]
assert vehicle["protocolVersion"] == "1.0"
assert vehicle["pluginCount"] == 1
assert vehicle["pluginMapping"] == {"rt-carplay SIMULATED": 0}
plugin, = vehicle["pluginConfigs"]
assert plugin["name"] == "rt-carplay SIMULATED" and plugin["pluginID"] == 0
accessory, = plugin["accessories"]
assert accessory["iid"] == 2560 and accessory["type"] == 0x09000005
service, = accessory["services"]
assert service["iid"] == 2561 and service["type"] == 0x1A00000F
characteristics = service["characteristics"]
assert len(characteristics) == 3
for index, (kind, initial) in enumerate(((0x30000024, 0), (0x3000004E, 6000), (0x3000004F, 5000))):
    characteristic = characteristics[index]
    assert characteristic["iid"] == 2562 + index
    assert characteristic["type"] == kind and characteristic["format"] == 3
    assert characteristic["initialValue"] == initial
    assert characteristic["minimumValue"] == 0
    assert characteristic["maximumValue"] == 6000 and characteristic["stepValue"] == 1
    assert characteristic["units"] == 981  # RPM, never road speed or battery SOC.
    assert characteristic["writable"] is False
    assert characteristic["mutable"] is (index == 0)
    assert characteristic["oemWritable"] is (index == 0)
assert "uiSyncInfo" not in info and "displayPanels" not in info
assert "hevcInfo" not in info
display, = info["displays"]
assert display["type"] == 110 and "hevcInfo" not in display
assert display["widthPixels"] == 1024 and display["heightPixels"] == 600
print("SIMULATED engine /info independent plistlib validation: PASS")
