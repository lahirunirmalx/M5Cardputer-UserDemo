# M5Cardputer-UserDemo-Plus
Enhanced Official Firmware for M5Cardputer

This is the enhanced version of the official firmware for the M5Cardputer.

Originally developed by [WuSiYu](https://github.com/WuSiYu/M5Cardputer-UserDemo-Plus).

### Improvements by WuSiYu:

- Added **arduino-esp32** as an ESP-IDF component for easier development.
  - Ported some native, messy code to Arduino libraries to avoid crashes.
- Ported [@cyberwisk's](https://github.com/cyberwisk) Cardputer WebRadio as an app:
  - Using a modified version of ESP8266Audio to support HTTPS and chunked streaming (e.g., qtfm.cn).
  - Ability to play radio in the background when pressing the HOME (G0) button. Press ESC to fully exit.
  - Added more radio stations (you can modify the radio list in `main/apps/app_radio/M5Cardputer_WebRadio.cpp`).
- Enhanced SCAN, TIMER (renamed to CLOCK), and SetWiFi apps.
- Added **SCALES** and **ENV IV** apps for the M5Stack mini-scales and ENV IV sensors.
- **System Enhancements**:
  - WiFi remains connected in the background by default. Open the SetWiFi app to disconnect.
  - Uses ESP-IDF’s automatic SNTP for time synchronization.
  - Enhanced system bar:
    - Shows battery voltage and current free heap size.
    - Added real functionality to the WiFi icon.

### Improvements in M5Cardputer-UserDemo:

- Removed **SCALES** and **ENV IV** apps.
- Improved code in WebRadio (still has minor bugs).
- System bar shows an icon if WebRadio is playing in the background.
- Default WiFi SSID and Password can be defined in `hal.h` (`main/hal/hal.h`) along with the user timezone (planned to be moved to user settings app later).
- Added FN key functionality to reset WiFi credentials in the SetWiFi app.

---
### Currently Working On

I'm actively improving the project with the following tasks:

- **Removing Unused Code**: Cleaning up the codebase by eliminating unnecessary and deprecated code snippets.
- **Adding Debug Logging and Comments**: Enhancing the code with detailed debug logs and comments for easier troubleshooting and better understanding.
- **Improving Code Structure**: Refactoring the code to follow best practices and improve readability and maintainability.
- **Introducing Dependency Management**: Implementing a structured approach for managing dependencies to ensure a smoother development and build process.
- **Documentation and Tutorial Videos**: Creating comprehensive documentation and tutorial videos to assist developers and users in understanding and using the project effectively.

These improvements aim to make the project more stable, user-friendly, and easier to maintain. Stay tuned for updates and new releases!
---
## To-Do List
Below is the roadmap of planned apps and enhancements:

- [ ] Screen Off / Charging Mode (to avoid screen burn-in).
- [x] Support for Built-in LED Notifications. [NeoLED](https://github.com/lahirunirmalx/NeoLED)
- [ ] Save and load user preferences.
- [ ] Calculator App.
- [ ] Resistor Calculator App.
- [ ] iPhone BLE Pairing Emulation.
- [ ] Google/Samsung BLE Pairing Emulation.
- [ ] TV-B-Gone App.
- [ ] Port [WinAmp](https://github.com/VolosR/M5Mp3).
- [ ] Fun games like [Tic-Tac-Toe](https://github.com/ZrutrA/tic-tac-toe-M5Cardputer) and [Snake](https://github.com/ZrutrA/game-snake-m5cardputer).
- [ ] File Manager App.
- [ ] Notification Center App.
- [ ] Port [GeminiPuter](https://github.com/nishad2m8/GeminiPuter) app.

---

## Toolchains

The project uses [ESP-IDF v4.4.6](https://docs.espressif.com/projects/esp-idf/en/v4.4.6/esp32/index.html).

---

## Build Instructions

To build the project, follow these steps:

```bash
git clone https://github.com/lahirunirmalx/M5Cardputer-UserDemo
```

```bash
cd M5Cardputer-UserDemo
```

```bash
idf.py build
```

---

## Assets

You can use the following tool to generate icons for the project. Note that you may need **Qt** to run this tool.

[lcd-image-converter](https://github.com/riuson/lcd-image-converter)

---

### Acknowledgments

Special thanks to WuSiYu for the original enhancements and contributions. We welcome further contributions and feedback from the community.

---