# Third-party notices

KlipperDeckZero itself is released under the MIT License (see [LICENSE](LICENSE)).
It includes or links the following third-party software and assets. The full license texts
are in [licenses/](licenses/) and [assets/fonts/LICENSE.txt](assets/fonts/LICENSE.txt), and the
Debian package installs them to `/usr/share/doc/klipper-deck-zero/`.

## Built into the application

| Component | Version | License | Copyright | License text |
|---|---|---|---|---|
| [CardputerZero/Template](https://github.com/CardputerZero/Template) | - | MIT | 2026 M5Stack CardputerZero Community | [LICENSE](LICENSE) |
| [LVGL](https://github.com/lvgl/lvgl) | 9.5.0 | MIT | 2025 LVGL Kft | [licenses/LVGL.txt](licenses/LVGL.txt) |
| Montserrat (LVGL built-in fonts) | - | OFL-1.1 | 2011 The Montserrat Project Authors | [licenses/Montserrat-OFL.txt](licenses/Montserrat-OFL.txt) |
| Font Awesome (symbols in the LVGL built-in fonts) | - | OFL-1.1 | Fonticons, Inc. | [licenses/FontAwesome.txt](licenses/FontAwesome.txt) |
| [IXWebSocket](https://github.com/machinezone/IXWebSocket) | 11.4.6 | BSD-3-Clause | 2018 Machine Zone, Inc. | [licenses/IXWebSocket.txt](licenses/IXWebSocket.txt) |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.11.3 | MIT | 2013-2022 Niels Lohmann | [licenses/nlohmann-json.txt](licenses/nlohmann-json.txt) |
| [{fmt}](https://github.com/fmtlib/fmt) | 10.1.1 | MIT | 2012 - present, Victor Zverovich and {fmt} contributors | [licenses/fmt.txt](licenses/fmt.txt) |

{fmt} is built in only when the device SDK does not provide it. Otherwise the package uses the
system `libfmt10`.

## Assets shipped with the package

| Asset | License | License text |
|---|---|---|
| [Phosphor Icons](https://phosphoricons.com/) (`assets/fonts/Phosphor-Fill.ttf`) | MIT | [assets/fonts/LICENSE.txt](assets/fonts/LICENSE.txt) |
| [Kenney Input Prompts](https://kenney.nl/) (`assets/fonts/kenney_*.ttf`, `kenny_*.ttf`) | CC0-1.0 | [assets/fonts/LICENSE.txt](assets/fonts/LICENSE.txt) |
| `assets/audio/click.wav`, `assets/images/cp0-*.png` (from CardputerZero/Template) | MIT | [LICENSE](LICENSE) |
| App icon (`assets/images/klipperdeck*.png`) | MIT, same as this project | [LICENSE](LICENSE) |

## Libraries used from the system

On the device, the following libraries are linked dynamically from the operating system and are
not shipped in the package: FreeType, libpng, libjpeg-turbo, zlib, the C/C++ runtime and
(when available) libfmt. The desktop simulator additionally uses SDL2 through vcpkg. These are
covered by their own licenses as distributed with the operating system or vcpkg.

## Not included

KlipperDeckZero talks to [Moonraker](https://github.com/Arksine/moonraker) over its public API.
It does not include code from Klipper or Moonraker (both GPL-3.0).
