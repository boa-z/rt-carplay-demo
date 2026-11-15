[English](wired-video-recovery.md) | [简体中文](zh-CN/wired-video-recovery.md)

# Wired video receive progress

## Evidence and limits

The latest user-supplied board trace reaches RTSP pairing, MFi authentication, screen setup, the first H.264 frame, visible video and stage 5. It contains an approximately 180 ms PCM timestamp hole (7920 samples at 44100 Hz), followed by several 330-sample gaps. No USB/per-frame performance windows were enabled. The log therefore confirms establishment and missing audio intervals, but does not measure screen FPS, decoder load, USB ACK latency or a unique cause of the reported stutter. Absence of an exception in this excerpt is not sustained stability acceptance. The attachment contains authentication material and is retained only in the ignored local artifact directory.

## Two reproduced receive dependencies

The portable IPHETH worker previously acquired tx_lock on every RX iteration, even with no halted endpoint. An independent synchronous Bulk OUT can then keep the sole RX URB idle after a completion. The new actual-worker regression blocks TX with a gate, completes RX, and requires another RX submission before TX is allowed to finish. The old code fails; the fixed code passes in both NCM and legacy modes. Normal RX now skips the TX mutex; a real TX STALL still acquires it, rechecks halt/stop state, clears the endpoint and resets the DATA toggle. Halt publication uses OSAL critical sections. USB timeouts, carrier checks, NCM validation, disconnect ownership and cancellation quarantine remain enforced.

The RT-Thread lwIP port also used LWIP_TCPIP_CORE_LOCKING_INPUT=1. The USB RX adapter copies each frame into an owned pbuf and invokes netif input; direct mode runs protocol input under the core lock in that same RX worker. TCP ACK output can synchronously wait for USB TX, delaying completion-block parsing and RX rearm. Both maintained CarPlay defconfigs now select RT_LWIP_QUEUED_INPUT, which uses standard lwIP mailbox input (LWIP_TCPIP_CORE_LOCKING_INPUT=0) while retaining LWIP_TCPIP_CORE_LOCKING=1. The TCP/IP thread processes the packet and ACK, while RX can rearm independently. The existing mailbox and input-message pools remain bounded; a full mailbox returns an error, and the adapter frees the rejected pbuf and counts rx_input_fail. Other targets retain direct input by default. No new DMA buffer, worker or custom networking queue is introduced.

The adapter reports input=queued or input=direct on first netif registration. This mode is platform policy; rt-carplay retains its existing portable interfaces, protocol and audio/video decoding paths. Neither dependency is established as the sole real-board cause, and no numerical FPS improvement is claimed.

## Executable verification

```sh
cmake -S packages/third-party/cherryusb-ipheth -B output/ipheth-host -DIPHETH_SANITIZE=ON -DCHERRYUSB_ROOT="$PWD/packages/third-party/cherryusb"
cmake --build output/ipheth-host -j8
ctest --test-dir output/ipheth-host --output-on-failure
make -C application/rt-thread/rtcarplay BUILD=build-network-validation -j8 check
```

Driver tests cover RX rearm during gated TX in NCM/legacy modes, TX and RX STALL recovery, independent Carrier delays, reconnect, allocation failures, multiple devices and failed-cancel retention. The application suite executes the actual lwIP input/dispatcher: direct mode fails the expected RX-context assertion, queued mode defers protocol input, queue-full leaves pbuf ownership with the adapter, dispatch-error frees it, and later input recovers. It also compiles the real lwipopts.h with GUI/Headless defconfigs and legacy/AIC8800 defaults, retaining the existing UDP slot/byte regressions. Existing route, audio, video, automatic owner, wired/wireless handover and lifecycle checks remain part of the full application check. GUI/Headless cross-builds and image identities are recorded separately in the delivered artifact report.

## Board comparison

Use the previous and new GUI images with the same phone, cable, display activity and audio workload. On each image, enable the existing five-second reporter before the measured workload:

```text
carplay_perf on
ipheth_status
carplay_connection status
carplay_usb net
```

Capture at least 60 seconds of continuous map movement or scrolling, first without music and then with music. Compare rx/show FPS, sender_gap, assembly/process, owner/partial gaps, queue/decode/present times, IPHETH rx_parse/rx_rearm and TX duration, and rx_input_fail/rx_errors. High sender gaps can reflect unchanged phone content; a stationary home screen is not a valid maximum-FPS test. Slow presentation with steady RX is different from gaps before frame assembly. Mailbox/pbuf failures or new packet loss invalidate a claim that queueing solved the problem.

Repeat USB reconnect and USB/Wireless switching, wireless audio/video, and Ethernet sharing. Test wired music for at least 15 minutes and retain any trap report for decoding with the matching ELF. The earlier EHCI completion-order repair remains present. The original PC/instruction discrepancy, physical echo, audio quality and separate inetshare timer-pool exhaustion remain open until separately verified. Do not infer that these are repaired by receive-path decoupling.
