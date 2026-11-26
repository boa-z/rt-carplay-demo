[English](design-system.md) | [简体中文](zh-CN/design-system.md)

# Cockpit design system

The local System Shell places a 40 px status bar and a 76 px navigation bar around the application container at the 1024×600 design size. Home shows vehicle speed and SOC alongside phone summaries. Phone Hub owns connection actions, Now Playing and route guidance. Phone details and System update are child routes. Settings groups related controls in a sidebar and detail panel.

The layout draws on the [AAOS small landscape reference](https://developers.google.com/cars/design/automotive-os/product-experience/system-ui/small-landscape) and [AOSP dual-pane settings](https://source.android.com/docs/automotive/hmi/car_settings/dual_pane). The preview exercises touch navigation at 800×480, 1024×600, 1280×720 and 1920×1080. The local surface currently scales the 1024×600 canvas at alternate resolutions.

| Graphite Blue token | Dark | Light |
| --- | --- | --- |
| Background | `#111214` | `#F2F3F5` |
| Surface | `#1B1C1F` | `#FFFFFF` |
| Secondary surface | `#25272B` | `#EDEEF1` |
| Primary | `#A8C7FF` | `#204F9F` |
| Primary container | `#233550` | `#DDE8FC` |
| Text | `#F2F3F5` | `#202227` |

Theme roles in `ui/carplay_ui_theme.c` control shared rows, tabs, sliders and state colors. The [font guide](../ui/fonts/README.md) covers glyph fallback and how to validate new translations.

The GUI reads copied app snapshots and submits actions through the existing owners. Home and Phone Hub reuse the same phone snapshot and decoded artwork descriptor. The cache clears when the live session or transport changes.

Fusion owns a separate local instrument strip. At 1024×600 the phone video occupies `x < 768`. Fullscreen projection hides the local shell. The Fusion preview checks the video rectangle when rendering the strip and switching themes.
