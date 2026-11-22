[English](ethernet-phy-verification.md) | [简体中文](zh-CN/ethernet-phy-verification.md)

# Ethernet PHY verification

## Report and cause

The user reported a known-good cable and router but dark Ethernet LEDs and Launcher showing Cable disconnected. The affected GUI image enabled GMAC0 with CLK_OUT2 disabled. After enabling CLK_OUT2, the user confirmed that Ethernet LEDs illuminate and board Internet access works. A later test also confirmed iPhone Internet service through the vehicle network. DHCP, cellular configuration and packet captures remain unrecorded.

The local D213EC-DEMO V4.0 schematic, sheets 1, 2 and 8, identifies U15 as RTL8201F, PE6 as reset, PE10/CLK_OUT2 as EPHY0-25M, and PE3 as MAC0_REFCLK. Sheet 8 connects the 25 MHz reference to the PHY, straps RMII mode and PHY address 1, and selects TXC output. The PHY supplies the separate 50 MHz RMII clock to the MAC. Selecting external RMII clock input alone does not enable the PHY reference output. The existing board sys_clk.c and pinmux.c enable the PE10 reference only when CLK_OUT2 is configured. Hardware documents remain local reference material and are not copied into the public source tree.

## Change

The existing GUI defconfig explicitly enables CLK_OUT2 at 25000000 Hz, RMII, external MAC clock and PHY address 1. Reset remains PE6. A board compile guard rejects GMAC0 with the reference disabled, a different reference frequency, or conflicting I2S0/DVP pins. The existing SDK clock and pinmux implementation is reused. No additional defconfig, SDK protocol changes or media/session changes are introduced.

## Local verification

| Check | Result | Evidence and boundary |
| --- | --- | --- |
| Effective GUI clock and pinmux | PASS | Cross-compiler preprocessing of actual board sources using the delivered configuration; 25 MHz clock entry and unique PE0 through PE10 MAC/reset/reference assignments. |
| Configuration guards | PASS | Missing clock, 50 MHz reference, I2S0 and DVP each rejected by the actual board source. |
| GUI and Headless complete images | PASS | Sequential local SCons builds, matching cached same-source Bootloader, nine IMG components with bounds and CRC32 verified. |
| Production lwIP route and supervisor | PASS | Two CTest executables under ASan/UBSan; modeled interfaces and sockets. |
| Actual LVGL renderer and events | PASS | Twelve previews and existing network/settings/device/audio/pairing event checks; modeled network values. |
| Ethernet LEDs and board Internet | PASS (user report) | User flashed the replacement GUI image and confirmed illuminated LEDs and working connectivity. No independent agent measurement or raw logs. |
| DHCP details and cable recovery | NOT_RUN | Not separately documented by the user; no serial port was enumerated on this host. |
| iPhone Internet services through the vehicle network | PASS (user report) | User confirmed functional access. Cellular-disabled routing evidence, phone/iOS identity and packet captures remain unrecorded. |
| Concurrent CarPlay load and network recovery | NOT_RUN | Prolonged audio/video/touch load, cable/WAN recovery and wired regression have not been separately reported for this image. |
| Remote CI | NOT_RUN | Local validation record; remote CI results are tracked separately after publication. |

## Artifact identity

The replacement GUI image is output/ethernet-phy-validation/gui/d213-carplay-gui.img. Its SHA256 is `5002e942799e8aaf5b3fe3ff827b9d67abd7e8db7f8afa9384517b69cd4d0468`. The Headless image SHA256 is `1110fe5b33c64c323cbefb2cea665755809055458d039518b158a0bc82b5af6b`. The directory contains configurations, Bootloader, image layout, firmware symbols, logs, a local source snapshot and checksums. The images were built from the recorded uncommitted source snapshot before commit authorization. Implementation commit dcb9a339 records that source with whitespace cleanup and the first hardware feedback; this subsequent documentation update records the user's iPhone success and authorization to commit and push the existing carplay-oss branch. No branch creation/switch or PR is involved.

## Board retest

1. Flash the replacement GUI IMG and record its hash. Boot with the known-good cable attached; observe Ethernet LEDs and open the Launcher Ethernet page.
2. Confirm link, DHCP address, gateway and DNS. Run Check now and record the result; physical link and Internet reachability are separate checks.
3. Unplug and reconnect Ethernet. Confirm stale success clears and link/address/connectivity recover. Repeat during wireless and wired CarPlay and record audio/video/touch behavior against the existing baseline.
4. If LEDs remain dark, capture the complete boot log, especially aicmac reset timeout, PHY MDIO errors and Port 0 link messages. Measure PE10 reference at 25 MHz, PE6 reset release and PHY 3.3 V supply before attributing the remaining fault to DHCP or iAP2. Verify the actual board revision against the schematic. These measurements have not been performed here.
