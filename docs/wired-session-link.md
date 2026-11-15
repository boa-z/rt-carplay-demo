# Wired USB session link policy

[中文](zh-CN/wired-session-link.md)

## Evidence

The supplied `1.txt` and `2.txt` test `epoch-after-scan` and `iaa-before-scan` respectively. Both complete pairing, CarKit TLS, MFi and identification, then repeatedly report `03 03 03 00`, zero RX completions, unchanged endpoint-86 qTD byte counts and tentative IPv6. Both reach the stage-3 network timeout and later report `MEMP_SYS_TIMEOUT` exhaustion during wireless recovery. The IRQ-order experiment did not restore connection. It does not identify an individual bad commit or prove a lost USB completion.

The current Linux-style ipheth policy requires positive vendor Carrier before link/TX. The earlier host-ready hint only enabled polling and still blocked on reply byte 3. The application's valid-IPv6 invitation gate then waits while lwIP cannot send DAD on a down link. This is a demonstrated software dependency, not proof that removing it restores this phone's connection.

Community reference [DiPlay 615b32ce](https://github.com/shihabal3amri/DiPlay/tree/615b32ce0b40cd0b99ce94ba23a512001553684f) operates a USB NCM bridge and host-configured IPv6 endpoint without the Linux Carrier gate. Its `IphoneUsbHost.kt` treats request `0x45` as a USB-mode diagnostic and mentions `3:3:3:0`; that request uses index zero whereas this driver's query uses the Ethernet interface index. This is not an official Apple status definition and does not justify relabelling every response containing 3 as Carrier-up. No reference implementation code was copied.

## Policy and ownership

After successful CarKit TLS open, the application enables optional `usbh_ipheth_session_link(hport, ready)` together with the existing host-ready hint. The board adapter checks the USB attachment generation under its critical section. Normal close revokes both after the iAP2 owner stops; failed owner stop retains live resources. Detach clears policy, and a replacement device cannot inherit a stale pairing result. The SDK has no new dependency.

The driver requires successful RX submission plus armed or observed receive readiness. An explicit external USB session then authorizes effective link/TX independently of vendor Carrier. Other hosts retain default Carrier-based behavior. Failed RX submission cannot enable link/TX. Query errors/down do not override an active external session. Raw Carrier and RX counters remain observational; `session_link` and `link_up` report policy and effective transport separately. Effective link is neither internet-connectivity evidence nor AirPlay acceptance.

lwIP still performs DAD, and the application still waits for a valid address before `0x4301`. The change does not guess an endpoint, force an address preferred, change alternate/NCM selection or modify SDK negotiation or media. Queued input and safe EHCI descriptor reclamation remain enabled.

The static timeout budget adds configurable `RT_LWIP_SYS_TIMEOUT_EXTRA`, default eight, to lwIP's own internal formula. Core accounting excludes per-interface mDNS probes, NAPT and the network monitor. Three current interfaces plus NAPT and the monitor fit with headroom; additional owners require budget review. This separately reproduced shortage is not presented as the initial zero-RX cause.

## Checks and board acceptance

ASan/UBSan driver checks cover silent NCM/Legacy startup with Carrier 3, hint-only rejection, explicit opt-in and actual TX, query failure without fabricated Carrier, revocation, wrong hport, reconnect and failed RX submission. The board-adapter test verifies attachment generation checks for both notifications. The real static lwIP allocator/scheduler test uses the actual port budget, runs 200 rearm cycles, releases all owners and reallocates every slot; its core-only negative control reproduces exhaustion. Protocol timer bodies are outside that allocator harness. A separate actual-lwIP IPv6 test verifies that link-down blocks DAD and enabling link sends NS before the address becomes valid. Application Host checks and the GUI build are software evidence only.

Use the GUI image and hash in `output/wired-session-link-current/VERIFICATION.md`. Its startup marker is `policy=usb-session-v1`. Diagnostics should show `session=enabled effective=up` while preserving the actual Carrier response. Record DAD NS/TX, valid IPv6, the session invitation, inbound TCP and first picture. If TX advances while RX remains zero, retain counters and EHCI snapshots for further receive-path investigation. Repeat cold boot/replug, then music, touch and wireless fallback/recovery. iPhone acceptance remains pending.

The final delivery uses a dedicated filesystem copy updated to parent `ceaa76c9`, preserving its committed GUI/OTA configuration, TF mount changes, OTA manifest and packaging script. The bootloader is rebuilt in that copy before GUI; neither generated configuration nor bootloader is copied back to the shared checkout. OTA packaging can be checked in software, but OTA installation and boot-slot switching still need board tests. The user removed redundant Headless firmware; this delivery contains only GUI and does not restore that configuration. Host config checks likewise require only the retained GUI configuration.

## Subsequent board failure

The next supplied log contains `policy=usb-session-v1`, valid IPv6, successful MFi/identification and acknowledged StartSession invitations, but no AirPlay TCP or picture. RX remains zero and Ethernet OUT repeatedly returns `-14` with zero actual bytes. CherryUSB defines 14 as `USB_ERR_TIMEOUT`; the `submit` counter includes failures after a synchronous wait and does not prove rejection before queuing. Carrier remains observed down (`03 03 03 00`). The exact flashed image hash is unknown, so the log identifies the exercised policy, not a particular artifact by hash. Effective link and DAD completion have not restored actual communication. Board acceptance is FAIL for this observation.

A subsequent live serial capture reproduces stage-4 timeout with the phone attached, RX/TX zero, 41 TX timeouts and no inbound TCP. Read-only commands and log capture work through the local AiBurn service on COM9. Raw logs remain in ignored local output because they contain device identities. Capture the OUT descriptors before cancellation on the next attempt: a later network-status snapshot cannot show a transfer that has already been cancelled. Preserve the queued input path, safe descriptor reclamation, OTA work and GUI-only configuration during this investigation.

## Attached-phone follow-up

The application now keeps an attached phone on the wired path after a session timeout, using the existing bounded 5–60 second backoff. It no longer restarts Wi-Fi between every such failure. Removal and other failures retain automatic wireless fallback. The Host scenario holds the phone attached through the full network/session wait and verifies another pairing attempt, cleanup of the previous owner and no intervening wireless start. This changes retry policy, not USB transfer readiness; the observed zero-RX failure still requires board diagnosis.

The generated EHCI source now invokes an optional weak snapshot hook before cancelling a synchronous timed-out URB. Diagnostics capture the first bulk OUT timeout per bus/address/endpoint, without payload data, before its QH is freed. Actual-HCD ASan/UBSan regression checks that the QH is still active at the hook, the original timeout result is preserved and cancellation frees the QH without a completion callback. This adds evidence, not a transfer fix. The next board image also provides application-owned [USB file transfer](usb-files.md); USB1 is now the sole Host bus, numbered 0, so use `usb_ehci_status` without a hardcoded bus 1.
