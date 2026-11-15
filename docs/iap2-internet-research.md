[English](iap2-internet-research.md) | [简体中文](zh-CN/iap2-internet-research.md)

# Use For Internet: iAP2 field audit

## Reviewed versions and outcome

Reviewed DiPlay master at `615b32ce0b40cd0b99ce94ba23a512001553684f` after fetching its origin, and the local rt-carplay SDK at `57936326c311850e006310e94d2aa9d711ba6a1e`. Research date: 2026-11-14. The SDK branch remains main and the application branch remains carplay-oss. No branch, PR, push or firmware change was made for this audit. See [Internet sharing](internet-sharing.md) for the existing implementation and physical acceptance requirements.

The named DiPlay discussion exists, but does not supply a missing field definition or implementation. The network-related fields in its current identification, Wi-Fi configuration and session-start encoders are already present in the SDK. This audit therefore does not establish a justified iAP2 field to add. The message ID, parameter/group ID, wire type, value and update rules for an additional Internet declaration remain **unconfirmed**. This is not proof that no such private protocol field exists; it is a boundary on what these sources demonstrate.

## Source and wire comparison

| Message / group | DiPlay network fields | SDK result |
| --- | --- | --- |
| IdentificationInformation 0x1D01, group 24 / 0x0018 | u16(0,1), string(1,SSID), void(2), u16(3,1), void(4), void(5) | Already present; network-group fixture matches all 39 bytes |
| AccessoryWiFiConfigurationInformation 0x5703 | Optional BSSID 0, SSID 1, passphrase 2, security u8 3, channel u8 4 | Already present; fixture with BSSID matches all 54 bytes |
| CarPlayStartSession 0x4301, wireless group 1 | SSID 0, passphrase 1, channel u8 2, repeated IP string 3, security u8 4 | Already present; one-address fixture matches all 60 bytes |

These are bounded comparisons of network fields, not complete identification-message equivalence. DiPlay also has other optional functionality; fields for location, vehicle data, SDK version, cluster assets or mutual authentication do not acquire an Internet meaning merely because they are optional. The WiFiSharing category name does not itself declare Internet routing. The numeric meanings beyond the reviewed source are not inferred here.

The local fixture invokes the actual SDK C encoders under ASan/UBSan. Its output is compared against expected wire bytes derived from the pinned DiPlay statements and scalar/string encoding. This does not execute the DiPlay Android application and is not an iPhone capture or proof of acceptance. Only synthetic SSID/password/identity values are used. The existing SDK identification, CarPlay-message and session Host tests were also rebuilt and passed (3/3).

## Issue evidence and limits

In Issue #393, a community commenter asks for a CarPlay-Network declaration during iAP2 handshaking without supplying a message ID, parameter ID, wire type or packet trace. Another commenter says to run Check For Internet Access on the iPhone first. The original reporter then posts a screenshot containing Use For Internet and closes the issue at 2026-10-07T09:39:23Z. The closing actor is the reporter; the closure event has no linked commit. This chronology does not prove what made the UI appear, but does not document a code change adding a field either. The screenshot is evidence of a UI state only, not of Ethernet routing or a particular protocol mechanism.

Issue #657 contains an owner explanation of iPhone route selection while the CarPlay session remains unchanged: an apparently usable car hotspot may become the Internet route. This is a maintainer interpretation of that report, not an official protocol definition or evidence of a specific TLV. Neither discussion justifies inventing an always-on Internet flag, reusing an unrelated parameter or changing the existing Apple vendor IE.

## Integration decision and next evidence

Retain the current iAP2 messages and the application-owned Ethernet/AP/DHCP/DNS/NAPT policy. Do not declare Internet merely because Ethernet has link or an address. The Launcher HTTP probe is a head-unit check and differs from the iPhone settings action named Check For Internet Access; clicking one does not invoke the other.

Before adding a field, obtain either a verifiable definition or a differential capture from the same known working accessory with Internet unavailable/available and with the iPhone toggle off/on. Record exact iOS version, iPhone UI state and times; compare 0x1D01, 0x5703, 0x4301, subsequent iAP2 messages, AP beacon/probe Apple Device IE / Networking IE, DHCP options, IPv6 router advertisements and reachability probes. These are candidate observation points, not confirmed requirements. Keep credentials and authentication material out of shared traces.

An eventual portable SDK extension should encode only the established field through an explicit host input. The application must own Ethernet/lwIP readiness, stale-result rejection and offline recovery. Actual iPhone traffic through Ethernet still requires cellular-disabled unique server requests correlated with WAN capture/server logs. Board tests and appearance of Use For Internet remain NOT_RUN for this delivery. No new image was built: the existing Launcher-network image is unchanged.

## Local research evidence

The complete clone remains in `C:/Project/Carplay/references/DiPlay`. Local evidence is in `C:/Project/Carplay/references/diplay-internet-review`: original issue JSON/timeline, inspected screenshot, actual-sdk-wire.txt, comparison.json, wire-audit.c, compare-wire.py, run-audit.sh and SDK build/test logs. The comparison covers only the table above. The independent source-derived fixture is research evidence, not a new production test framework.

## Pinned references

- [IdentificationInformation](https://github.com/shihabal3amri/DiPlay/blob/615b32ce0b40cd0b99ce94ba23a512001553684f/shared/src/main/java/com/shilapi/xcertplay/transport/Iap2IdentificationClient.kt#L371).
- [Wi-Fi and CarPlay message codecs](https://github.com/shihabal3amri/DiPlay/blob/615b32ce0b40cd0b99ce94ba23a512001553684f/shared/src/main/java/com/shilapi/xcertplay/iap2/message/Iap2Messages.kt#L77).
- [Scalar and string wire codecs](https://github.com/shihabal3amri/DiPlay/blob/615b32ce0b40cd0b99ce94ba23a512001553684f/shared/src/main/java/com/shilapi/xcertplay/iap2/wire/Iap2WireCodec.kt#L13).
- [Issue #393](https://github.com/shihabal3amri/DiPlay/issues/393).
- [Issue #657 owner comment](https://github.com/shihabal3amri/DiPlay/issues/657#issuecomment-6118129967).
