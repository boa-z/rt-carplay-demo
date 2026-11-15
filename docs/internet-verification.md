[English](internet-verification.md) | [简体中文](zh-CN/internet-verification.md)

# Internet sharing verification

## Source and scope

This report covers the uncommitted local Internet-sharing changes on `carplay-oss`, based on `b933ef756afd6c9498d99d016482693b45f048b0`. The base commit alone does not identify these firmware images. The local delivery directory `output/internet-validation/` contains a changed-source archive, binary patch, per-file source hashes, configuration copies, matching Bootloader, image layouts, raw build/test logs and artifact hashes. No branch, pull request, push or remote repository change was performed for this task.

The portable SDK remains at `57936326c311850e006310e94d2aa9d711ba6a1e`; IPHETH remains at `2c87b69fda0d2dbeeb9df5180dd10157791d7371`. Neither submodule was modified. The application/network implementation and protocol evidence are described in [Ethernet Internet sharing](internet-sharing.md).

## Software checks

| Check | Result | Evidence and limits |
| --- | --- | --- |
| Actual production lwIP routing and application supervisor | PASS | Two CTest executables under ASan/UBSan, including TCP/UDP/ICMP round trips, checksums, interface isolation, allocation failure, stale probe rejection, HTTP/DHCP policy and recovery. RT-Thread/socket boundaries are modeled for the supervisor. |
| Existing application regression suite | PASS | Actual-source connection, Launcher, media, audio-test, pairing, video handoff and USB checks via the complete Host command. This does not establish sound quality or board rendering. |
| Same-source baremetal Bootloader | PASS | Build completed; both firmware variants carry the matching binary. |
| Bilingual documentation and static checks | PASS | Paired heading/code/literal/link/prose checks, SConscript AST, shell syntax and workflow YAML/matrix checks. |
| Existing GUI complete firmware, Ethernet and sharing enabled | PASS | Local cross-build, linked NAPT symbols and 9 IMG components with bounds/CRC verification. |
| Headless complete firmware, sharing disabled | PASS | Local cross-build, generic Headless example object, absent NAPT symbols and 9 IMG components with bounds/CRC verification. |
| Remote CI | NOT_RUN | Workflow definitions updated locally; no remote workflow was triggered. |

```sh
make -C application/rt-thread/rtcarplay -j8 check
cmake --build /home/boa/build-carplay-internet -j8
ctest --test-dir /home/boa/build-carplay-internet --output-on-failure
```

The Host routing capture `output/internet-validation/internet-routing.pcap` contains modeled ingress and actual lwIP output. It is not a board Ethernet capture. CI definitions preserve the corresponding Host capture and retain the existing GUI/Headless matrix; no extra Internet defconfig or firmware variant is added.

## Image identity

The existing GUI defconfig now enables Ethernet and optional sharing directly on the preceding version. Headless remains the compatibility regression with sharing disabled. Each local artifact directory contains `firmware.config`, `bootloader.bin`, `image-layout.json` and `SHA256SUMS`. Build configuration files are generated separately and profiles are compiled sequentially.

Cross-builds ran from the source copy at `/home/boa/carplay-internet-verify` on the WSL Linux filesystem to avoid Windows-mounted build overhead. All changed source files were checked byte-for-byte against the designated checkout. The first GUI link exposed the disabled Ethernet RX worker; restoring the SDK worker fixed the dependency without a new receive implementation. Final builds completed successfully. The temporary separate Internet configuration was removed and its interrupted build is not a delivered artifact. The designated checkout is left with the GUI configuration selected. The generated tracked Bootloader was restored after preserving the matching built copy in the artifacts.

| Variant | IMG SHA256 |
| --- | --- |
| GUI with Ethernet sharing | `068087bd4a0536385f58badad3053b2e82013dd91368068031e482c46bc5a96d` |
| Headless | `1335f773c7e8716b98294f1eb27f82647f26d39ad5d110a4d0a90282514c24c3` |

## Hardware acceptance and remaining work

Ethernet PHY configuration, board DHCP/probe, real iPhone Internet routing, simultaneous media/touch load and cable/outage recovery are all **NOT_RUN** for this source. No accessible board serial port or flashing configuration was found during this task. The acceptance procedure requires cellular-disabled iPhone traffic, a unique request to a controlled public server and correlated Ethernet capture/server logs; see the detailed guide. Standard DHCP/NAPT operation in software does not prove that iOS chooses this route or that no private capability mechanism is required.

The user's preceding-build observations remain baseline reports: wireless CarPlay works, wired projection displays with known stutter, and wired music is recognizable but poor, somewhat better than wireless audio. No source/image hash or raw log accompanies those reports. This implementation does not modify the audio algorithms, media/session code or Apple IE/iAP2 behavior, and does not claim to repair the known audio/stutter issues.

IPv4 fragments/options, IPv6 sharing and general remote ICMP-error translation are unsupported. Actual MTU/PMTU behavior, QUIC, table pressure, long idle connections and multi-client ICMP identifiers need further validation. DHCP router/DNS changes may take until lease renewal to reach the phone. The configurable HTTP 204 probe can be blocked or spoofed and is not an authenticated test of the entire Internet.
