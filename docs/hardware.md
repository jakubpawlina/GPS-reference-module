# Hardware

## Bill of materials

| Qty | Component | Notes |
|-----|-----------|-------|
| 1 | ESP32-D0WD-V3 dev board, 38-pin | Revision 3.1, CP2102 USB bridge |
| 1 | GPS receiver module | NMEA 0183, 9600 baud - e.g. Neo-6M, Neo-8M, Neo-M8N |
| 1 | 0.96" SSD1306 OLED, 128×64, I2C | 4-pin breakout (GND/VCC/SCL/SDA); must have onboard 4.7 kΩ pull-ups |
| 4 | Standard 5 mm LED | Red, blue, yellow, green |
| 4 | Resistor 220-330 Ω | One per LED |
| 1 | Raspberry Pi 4 Model B | Serves the REST/SSE API over Ethernet |
| 1 | USB cable, micro-USB to USB-A | ESP32 ↔ RPi (check your board - some use USB-C) |

---

## ESP32 pin assignments

All firmware constants are in `PinConfig` and `GpsConfig` namespaces in
[`src/firmware_settings.h`](../firmware/gps_reference_module/src/firmware_settings.h).

```
GPIO  Function          Direction  Wire colour (suggestion)
──────────────────────────────────────────────────────────
  16  GPS RX            IN         GREEN   (from GPS TX)
  17  GPS TX            OUT        WHITE   (to GPS RX - optional)
  19  OLED SDA          I/O        YELLOW
  22  OLED SCL          OUT        ORANGE
  23  LED red  (error)  OUT        RED
  21  LED blue (data)   OUT        BLUE
   5  LED yellow        OUT        YELLOW  (use a different shade from SDA)
  25  LED green         OUT        GREEN
```

**Power pins used:**
```
3V3  →  OLED VCC, GPS VCC (if 3.3 V module)
5V   →  GPS VCC (if 5 V module)
GND  →  OLED GND, GPS GND, all LED cathodes
```

---

## Physical pin adjacency - critical warnings

On a standard 38-pin ESP32 DevKit the left header runs (top → bottom):

```
3V3 · GND · 15 · 2 · 0 · 4 · 16 · 17 · 5 · 18 · 19 · 21 · RX · TX · 22 · 23
```

Key adjacencies affecting this design:

| GPIO | Function | Left neighbour | Right neighbour |
|------|----------|----------------|-----------------|
| 17 | GPS TX | GPIO16 (GPS RX) | **GPIO5 (yellow LED)** |
| **5** | **Yellow LED** | GPIO17 (GPS TX) | GPIO18 (unused) |
| 18 | *unused* | GPIO5 (yellow) | **GPIO19 (OLED SDA)** |
| **19** | **OLED SDA** | GPIO18 (unused) | **GPIO21 (blue LED)** |
| **21** | **Blue LED** | GPIO19 (OLED SDA) | GPIO3 / RX |

Because of this the firmware uses a **pin-specific off-state strategy**:

| LED | GPIO | Off state | Reason |
|-----|------|-----------|--------|
| Red | 23 | `INPUT_PULLDOWN` | Adjacent to GPIO22 (SCL) - OUTPUT LOW would pull SCL low |
| Blue | 21 | `INPUT_PULLDOWN` | Adjacent to GPIO19 (SDA) - OUTPUT LOW would pull SDA low |
| Yellow | 5 | `OUTPUT LOW` | Adjacent to GPIO17 (GPS_TX only) - not an I2C pin; OUTPUT LOW is safe |
| Green | 25 | `OUTPUT LOW` | Right side of board, no I2C neighbours |

Three off-state candidates were evaluated:

- **`OUTPUT LOW`** - sinks current from adjacent lines.  Safe for GPIO5 and
  GPIO25 (neighbours are GPS_TX and unused pins).  Breaks I2C for GPIO21/GPIO23
  because SDA/SCL have only 4.7 kΩ pull-ups that OUTPUT LOW can easily override.
- **`INPUT` (floating)** - driven neighbours leak enough current into the
  floating pin to dimly illuminate the LED.  Rejected for all pins.
- **`INPUT_PULLDOWN`** (~47 kΩ to GND) - too weak to overcome GPS_TX (GPIO17)
  idling continuously at 3.3 V, which caused visible dim glow on the yellow LED.
  Safe for I2C-adjacent pins because 47 kΩ << 4.7 kΩ pull-up impact is negligible.

---

## OLED display

**Module:** 0.96" SSD1306, 128×64, I2C, 4-pin breakout.
Standard pin order (left → right when connector faces you): GND | VCC | SCL | SDA.

```
OLED          →  ESP32          Wire
──────────────────────────────────────
GND           →  GND            BLACK
VCC           →  3V3            RED     ← 3.3 V only; 5 V will destroy the module
SCL           →  GPIO22         ORANGE
SDA           →  GPIO19         YELLOW
(no RESET pin - firmware uses RESET_PIN = -1)
```

- The module must have onboard **4.7 kΩ pull-up resistors** on SDA and SCL.
  Most 4-pin SSD1306 breakouts sold as "I2C OLED" include them; bare SSD1306
  chips do not.
- **I2C address:** 0x3C (SA0 pin tied LOW).  The firmware auto-scans 0x3C
  then 0x3D and reports the found address in the `startup` JSON message.

---

## GPS receiver

**Protocol:** NMEA 0183.  **Baud rate:** 9600 (fixed in firmware).

Tested with Neo-6M, Neo-8M, and Neo-M8N modules.  Any module that outputs
standard GGA, GSA, and RMC sentences at 9600 baud will work.

```
GPS module    →  ESP32          Wire
──────────────────────────────────────
GND           →  GND            BLACK
VCC           →  see below      RED
TX            →  GPIO16         GREEN   (ESP32 receives NMEA)
RX            →  GPIO17         WHITE   ← optional; firmware sends no commands
```

**VCC voltage:**

| Module variant | VCC |
|----------------|-----|
| Neo-6M bare chip / most breakouts | 3.3 V (ESP32 3V3 pin) |
| Neo-6M with onboard 5 V regulator (blue PCB) | 5 V (ESP32 5V/VIN pin) |
| Neo-8M, Neo-M8N | 3.3 V or 5 V - check module datasheet |

> Connecting a 3.3 V-only module to 5 V will destroy it.  When in doubt,
> use 3.3 V.

The GPS RX wire to GPIO17 is optional - the firmware never sends commands to
the module.  You can leave GPIO17 disconnected.

---

## LEDs

**Circuit (active HIGH):** ESP32 GPIO → resistor → LED anode → LED cathode → GND.

```
ESP32 GPIO  →  R (220-330 Ω)  →  LED (+)  →  LED (-)  →  GND
```

| LED    | GPIO | Wire   | When on | Off state |
|--------|------|--------|---------|-----------|
| Red    | 23   | RED    | No data / no fix | `INPUT_PULLDOWN` - adjacent to SCL (GPIO22) |
| Blue   | 21   | BLUE   | Fresh NMEA being received from GPS | `INPUT_PULLDOWN` - adjacent to SDA (GPIO19) |
| Yellow | 5    | YELLOW | Degraded fix (2D-only or fewer than 6 satellites) | `OUTPUT LOW` - adjacent to GPS_TX (GPIO17) only |
| Green  | 25   | GREEN  | Full 3D fix with ≥6 satellites (REFERENCE_OK) | `OUTPUT LOW` - no I2C neighbours |

LED combinations by state:

| State | Red | Blue | Yellow | Green |
|-------|:---:|:----:|:------:|:-----:|
| NO_GPS_DATA | ● | | | |
| NO_FIX | ● | ● | | |
| DEGRADED_2D / DEGRADED_LOW_SAT | | ● | ● | |
| REFERENCE_OK | | ● | | ● |

---

## ESP32 ↔ Raspberry Pi

**Preferred: USB cable (simplest)**

```
ESP32 micro-USB  ──[USB cable]──  RPi USB-A port
```

The CP2102 bridge on the ESP32 dev board enumerates as `/dev/ttyUSB0` on the
RPi automatically.  No drivers or configuration needed.

**Alternative: GPIO UART (3 wires)**

Only needed if the USB port is unavailable.

```
ESP32            Wire    →  RPi physical pin
────────────────────────────────────────────
GPIO1 (TXD0)     YELLOW  →  Pin 10  (GPIO15 / RXD)
GND              BLACK   →  Pin 6   (GND)
VIN (5 V in)     RED     →  Pin 2   (5 V)
```

Change `GPS_SERIAL_PORT=/dev/ttyAMA0` in the service config file and disable
the RPi serial console via `raspi-config` (Interface Options → Serial Port →
disable login shell, enable hardware port).

---

## Pins to avoid

The following ESP32 GPIOs affect boot behaviour and must not be connected to
anything that drives them during power-on:

| GPIO | Boot constraint |
|------|----------------|
| 0 | Must be HIGH (floating/pull-up) during normal boot; LOW enters download mode |
| 2 | Must be LOW or floating during boot (onboard LED on some boards) |
| 12 | Must be LOW during boot; HIGH selects 1.8 V flash voltage |
| 15 | Must be HIGH during boot to suppress boot log output |

None of these are used in this design.

---

## Full connection summary

| From | To | Wire | Notes |
|------|----|------|-------|
| ESP32 GPIO16 | GPS TX | GREEN | NMEA input |
| ESP32 GPIO17 | GPS RX | WHITE | Optional |
| ESP32 GPIO19 | OLED SDA | YELLOW | I2C data |
| ESP32 GPIO22 | OLED SCL | ORANGE | I2C clock |
| ESP32 GPIO23 | LED red (+) | RED | Via 220-330 Ω |
| ESP32 GPIO21 | LED blue (+) | BLUE | Via 220-330 Ω |
| ESP32 GPIO5 | LED yellow (+) | YELLOW | Via 220-330 Ω |
| ESP32 GPIO25 | LED green (+) | GREEN | Via 220-330 Ω |
| ESP32 3V3 | OLED VCC | RED | 3.3 V only |
| ESP32 3V3 or 5V | GPS VCC | RED | See GPS VCC table |
| ESP32 GND | OLED GND | BLACK | |
| ESP32 GND | GPS GND | BLACK | |
| ESP32 GND | All LED cathodes | BLACK | Common ground |
| ESP32 USB | RPi USB-A | - | Data + power for ESP32 |
