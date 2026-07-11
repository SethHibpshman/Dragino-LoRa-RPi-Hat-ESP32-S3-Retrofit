# dragino-lora-hat-esp32s3-retrofit

Custom [MeshCore](https://github.com/meshcore-dev/MeshCore) firmware and PCB carrier for running a **Dragino LoRa/GPS HAT** on an **ESP32-S3** instead of the Raspberry Pi it was designed for.

![Assembled node](images/assembled_project.png)

---

## What this actually is

The Dragino LoRa GPS HAT is built for the Raspberry Pi 40-pin GPIO header. It has no business working with an ESP32-S3 dev board. There's no official pinout for it, no example firmware, and none of the pin numbers on the HAT correspond to anything on an ESP32.

I designed a custom carrier PCB that reroutes the Dragino HAT's SPI, IRQ, and GPS UART lines to an ESP32-S3-DevKitC-1-N16R8, wrote a new MeshCore board target from scratch to drive that hardware, and got it running as three different mesh network node roles: a headless GPS repeater, a Bluetooth companion radio, and a USB-wired companion radio.

Everything here, the PCB, the case, and the firmware, was built to get an off-the-shelf Raspberry Pi accessory running on a completely different microcontroller architecture.

---

## The hardware

### Custom carrier PCB

![PCB layout](images/pcb.png)

The Dragino HAT header doesn't line up with anything on the ESP32-S3, so I designed a board in KiCad that:

- Breaks out both 24-pin rows of the ESP32-S3-DevKitC-1 header
- Routes SPI (SCK/MOSI/MISO), the three LoRa IRQ lines (DIO0/1/2), NSS, and RESET down to the Dragino HAT's Pi-header footprint in the correct positions
- Routes the GPS UART (TX/RX) and I2C (SDA/SCL) separately, since the L80 GPS module and the SSD1306 display need to share the board without colliding with the SPI bus
- Adds a 4-cell battery holder array, a power switch, a buzzer, and a footprint for the OLED and user button, so the whole companion radio build (display, button, buzzer, GPS, LoRa) lives on one board instead of a nest of jumper wires

### 3D-printed case

![Case render](images/case_render.png)

Modeled a low-profile tray enclosure to hold the stacked board assembly, sized to the footprint of the carrier PCB with room for the antenna connector and button access.

---

## What I actually had to figure out

This wasn't a drop-in board support file. Getting MeshCore to run correctly on this combination of parts meant working through a handful of problems that don't show up on boards MeshCore already supports:

**PSRAM pin conflicts.** The N16R8 variant of the ESP32-S3 uses GPIO 35, 36, and 37 internally for OPI PSRAM. Every wiring decision on the carrier board had to route around those three pins, since using them for anything else causes a silent crash on boot.

**No board definition existed for this chip variant.** PlatformIO's stock `esp32-s3-devkitc-1` board JSON doesn't know about the R8 OPI PSRAM, so the board fails with a PSRAM ID error before anything else runs. I wrote a custom `boards/dragino_esp32s3_n16r8.json` with `qio_opi` memory type set explicitly.

**Building a MeshCore target from nothing.** MeshCore expects a `target.h` / `target.cpp` / board class for every supported piece of hardware. There wasn't one for this pairing, so I wrote:
- `DraginoESP32S3Board.h`, a board class that reports the correct IRQ GPIO to the radio driver
- `target.h` / `target.cpp`, wiring up the SX1276 radio driver, the RTC clock with autodetect fallback, the GPS NMEA provider, and the SSD1306/button peripherals conditionally based on which node role is being built

**I2C had to be moved off the default pins by hand.** The ESP32 Arduino core defaults `Wire` to GPIO 21/22, which isn't where the SSD1306 is wired on this board. `Wire.begin(8, 9)` gets called explicitly in `radio_init()`, and the OLED's reset pin (default GPIO 21 in MeshCore) gets overridden to `-1` to avoid landing on the same pin as the display bus.

**Three node roles from one codebase, decided at compile time.** MeshCore can build as a repeater, a BLE companion radio, or (with a small addition I made to the variant's `platformio.ini`) a wired USB companion radio. The BLE-vs-USB choice comes down to whether `BLE_PIN_CODE` is defined at build time: if it is, `main.cpp` compiles in `SerialBLEInterface`; if it isn't, it falls through to plain `ArduinoSerialInterface` and the companion protocol runs over native USB serial instead. Same firmware logic, same wiring, different transport, chosen by which PlatformIO environment you flash.

---

## Node roles

| Role | Connectivity | Use case |
|---|---|---|
| **Repeater** | None (headless) | Deploy high, leave powered, extends mesh range, beacons GPS |
| **Companion Radio (BLE)** | Bluetooth to phone | Carry with you, OLED + button + buzzer, pairs with the MeshCore app |
| **Companion Radio (USB)** | Wired serial | Same features as BLE, plugs into a PC or SBC instead of pairing |

All three are built from the same variant and the same wiring; the only thing that changes is which PlatformIO environment gets flashed.

---

## Wiring (LoRa/GPS, required for every role)

| Dragino HAT Pin | Function | ESP32-S3 GPIO |
|---|---|---|
| 3.3V | Power | 3V3 |
| GND | Ground | GND |
| LoRa_NSS | SPI CS | GPIO 5 |
| LoRa_MOSI | SPI MOSI | GPIO 11 |
| LoRa_MISO | SPI MISO | GPIO 13 |
| SCK | SPI Clock | GPIO 12 |
| DIO0 | LoRa IRQ | GPIO 4 |
| DIO1 | LoRa IRQ | GPIO 6 |
| DIO2 | LoRa IRQ | GPIO 7 |
| RESET | LoRa Reset | GPIO 14 |
| GPS_TXD | GPS data out | GPIO 17 |
| GPS_RXD | GPS data in | GPIO 18 |

Companion-radio-only wiring (SSD1306, button, buzzer) is on the PCB directly; see `pcb.png` above for the full layout including I2C and GPIO assignments.

> **Antenna warning:** always connect the 915MHz antenna before powering on. Transmitting without one can permanently damage the SX1276.

---

## Building it

```powershell
git clone https://github.com/meshcore-dev/MeshCore.git
cd MeshCore
# copy in this repo's variants/dragino_esp32s3/ and boards/dragino_esp32s3_n16r8.json

& "$HOME\.platformio\penv\Scripts\pio.exe" run -e dragino_esp32s3_repeater --target upload
& "$HOME\.platformio\penv\Scripts\pio.exe" run -e dragino_esp32s3_companion_radio_ble --target upload
& "$HOME\.platformio\penv\Scripts\pio.exe" run -e dragino_esp32s3_companion_radio_usb --target upload
```

Any of the three roles can be reflashed onto the same hardware at any time, the firmware overwrites completely.

---

## File structure

```
MeshCore/
├── boards/
│   └── dragino_esp32s3_n16r8.json     ← custom board def, fixes PSRAM crash
└── variants/
    └── dragino_esp32s3/
        ├── DraginoESP32S3Board.h      ← board IRQ class
        ├── target.h                   ← hardware declarations
        ├── target.cpp                 ← hardware implementation
        └── platformio.ini             ← repeater / BLE / USB build environments
```

---

## Stack

ESP32-S3-DevKitC-1-N16R8 · Dragino LoRa/GPS HAT (SX1276, 915MHz, L80 GPS) · SSD1306 OLED · MeshCore · PlatformIO · RadioLib · KiCad · custom 3D-printed enclosure

---

*Built and tested June–July 2026. MeshCore (meshcore-dev/MeshCore), PlatformIO espressif32@6.11.0, RadioLib 7.7.1.*