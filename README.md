# ESP32-S3 PlatformIO Project

This project targets the Espressif ESP32-S3-DevKitC-1 using the Arduino framework.

## Build and upload

Open this folder in VS Code with the PlatformIO IDE extension installed, then use the PlatformIO actions to build, upload, or open the serial monitor.

The firmware implements a 4 x 3 macro-key grid, two rotary encoders with push-to-mute, and a 240 x 320 ST7789 SPI display.

## Wokwi Hardware Visualizer

The project includes a Wokwi Hardware Visualizer circuit in `diagram.json`. It models the ESP32-S3-DevKitC-1, twelve macro buttons, both rotary encoders, and the SPI display using the pin assignments in `src/main.cpp`. The `wokwi.toml` file points the visualizer to the PlatformIO build output.

Install the Wokwi for VS Code extension, build the project, then open the Command Palette and run **Wokwi: Start Simulator**. Use the buttons and rotary encoders in the visualizer to test the macro inputs and volume display behavior.

## Assumed wiring

The GPIO map is at the top of `src/main.cpp`. It assumes active-low switches wired to ground with the internal pull-ups enabled:

- Keys: GPIO 4 through 15, left-to-right then top-to-bottom
- Encoders: `{A, B, push}` on `{16,17,18}`, `{37,36,35}`
- Display: SCK 43, MOSI 44, CS 1, DC 2, RST 3

Change the pin map if it does not match the PCB. GPIO 19 and 20 are intentionally left unused for native USB.

## Key behavior

The default key macros are Windows-friendly shortcuts in `runMacro()`. Replace those cases with the shortcuts used by the host applications. HID can launch programs through an OS shortcut such as `Win+R`, but it cannot launch a desktop executable directly without a host-side launcher.

### Macro buttons

The buttons are assigned left-to-right, top-to-bottom:

| Button | Macro |
| --- | --- |
| 1 | Calculator |
| 2 | Steam |
| 3 | Visual Studio Code |
| 4 | Bambu Studio |
| 5 | LinkedIn |
| 6 | Copilot |
| 7 | Signal |
| 8 | Teams |
| 9 | PowerPoint |
| 10 | Word |
| 11 | Excel |
| 12 | Outlook |

Each encoder sends a consumer-control volume increment/decrement per detent. Its push switch sends mute and updates the display state.
