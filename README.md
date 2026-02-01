```
███████╗██╗ ██████╗ ███╗   ██╗ █████╗ ██╗         ██████╗ ███████╗
██╔════╝██║██╔════╝ ████╗  ██║██╔══██╗██║         ██╔══██╗██╔════╝
███████╗██║██║  ███╗██╔██╗ ██║███████║██║         ██████╔╝█████╗
╚════██║██║██║   ██║██║╚██╗██║██╔══██║██║         ██╔══██╗██╔══╝
███████║██║╚██████╔╝██║ ╚████║██║  ██║███████╗    ██║  ██║██║
╚══════╝╚═╝ ╚═════╝ ╚═╝  ╚═══╝╚═╝  ╚═╝╚══════╝    ╚═╝  ╚═╝╚═╝
```

**Multi-Tool RF Analysis Firmware for M5Stack Cardputer**

---

## Quick Start

1. Download `SignalRF_Default.bin` from the [SignalRF folder](SignalRF/)
2. Flash to your Cardputer:
   ```bash
   esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write_flash 0x0 SignalRF_Default.bin
   ```
3. Press any key to enter the main menu

That's it. No libraries to install, no code to compile.

---

## What It Does

Signal RF turns your M5Stack Cardputer into a portable RF analysis toolkit. Five tools in one firmware:

| Tool | What It Does |
|------|--------------|
| **WiFi Scanner** | Scan networks, view signal strength, track RSSI over time |
| **BLE Scanner** | Find Bluetooth devices, identify vendors, monitor signals |
| **Ultrasonic Detector** | Real-time spectrum analyzer, detects frequencies above 18kHz |
| **Signal Mapper** | Map WiFi/BLE coverage across a 5x5 grid |
| **Tracker Detector** | Find AirTags, Tiles, and other tracking devices near you |

---

## Features

- **Color-coded signal strength** - Green (strong), Yellow (medium), Red (weak)
- **New device alerts** - Audio notification when new devices appear
- **RSSI graphing** - Track signal strength over time in device detail view
- **Vendor lookup** - Identify device manufacturers via OUI database (optional)
- **Tracker detection** - Scan for AirTags, Tile, Samsung SmartTags
- **Ultrasonic alerts** - Detect hidden ultrasonic transmitters
- **Battery indicator** - Monitor your Cardputer's battery level
- **SD card support** - Load OUI database for vendor identification

---

## Controls

All tools use simple keyboard controls:

| Key | Action |
|-----|--------|
| `1-5` | Select tool from main menu |
| `W/S` | Navigate up/down |
| `Enter` | Select / View details |
| `R` | Rescan |
| `Backspace` | Back / Return to menu |

See [CONTROLS.md](SignalRF/CONTROLS.md) for complete control reference.

---

## Hardware Requirements

- **M5Stack Cardputer** (ESP32-S3)
- **MicroSD Card** (optional) - For OUI vendor database
  - FAT32 formatted
  - 64GB or smaller recommended

---

## OUI Database Setup (Optional)

The OUI database enables vendor/manufacturer lookup for MAC addresses. Signal RF works fine without it - you just won't see vendor names.

### Getting the Database

Download the IEEE OUI database and convert it, or create your own `oui.txt`:

```
AABBCC|Apple Inc
001A2B|Samsung Electronics
```

Format: `MAC_PREFIX|Vendor Name` (one per line, no colons in MAC)

### Installation

1. Format microSD as FAT32
2. Place `oui.txt` in the root directory
3. Insert into Cardputer

---

## Building From Source

If you want to modify the firmware:

### Prerequisites
- Arduino IDE or arduino-cli
- M5Stack board support package
- Libraries: M5Cardputer, WiFi, BLEDevice, arduinoFFT, SD

### Compile & Flash

1. Open `SignalRF/SignalRF.ino` in Arduino IDE
2. Select board: M5Stack Cardputer
3. Compile and upload

---

## Themes

Signal RF includes a theme system. Edit `SignalRF.ino` to switch:

```cpp
// Uncomment ONE theme:
//#define THEME_DEFAULT
#define THEME_SASQUATCH
```

---

## Tips

- **Find WiFi dead zones** - Use Signal Mapper to walk around and sample coverage
- **Detect tracking devices** - Tracker Detector can find unknown BLE devices following you
- **Track signal changes** - In Device Detail view, press `R` repeatedly while moving
- **Find hidden transmitters** - Ultrasonic Detector can identify ultrasonic beacons

---

## Acknowledgments

This project was built on the shoulders of the open source community. Special thanks to:

- **[Bruce](https://github.com/pr3y/Bruce)** - A major inspiration for this project. The Bruce firmware showed what's possible on the Cardputer platform and motivated me to contribute something back to this community.
- **M5Stack** - For creating the Cardputer and maintaining excellent Arduino libraries
- **The ESP32 community** - For the countless libraries and examples that make projects like this possible
- Everyone who shares their code openly - you make it possible for newcomers to learn, build, and contribute

This is my first release to the community. I hope Signal RF is useful to you. Fork it, modify it, make it your own.

---

## Contributing

Pull requests welcome. If you build something cool with this, I'd love to see it.

---

## License

MIT License - Free to use, modify, and distribute. See [LICENSE](LICENSE).

---

## Author

**Byte**

Built for the M5Stack Cardputer community.
