# Pet Feeder

Arduino Uno/Nano automatic cat feeder with DS1302 RTC and L298N motor driver.

## Features

- Programmable feeding schedule (default: 08:00, 13:00, 20:00)
- Manual feed via TEST button
- Portion size adjustment via 100kΩ potentiometer
- Non-blocking motor control (no `delay()` in main loop)
- Serial monitor output for diagnostics

## Pin Mapping

| Pin  | Connection           |
|------|----------------------|
| D10  | DS1302 CLK (SCLK)   |
| D11  | DS1302 DAT (I/O)    |
| D12  | DS1302 RST (CE)     |
| D6   | L298N IN1           |
| D7   | L298N IN2           |
| D2   | TEST button (INPUT_PULLUP) |
| A0   | 100kΩ potentiometer |

DS1302 VCC → 5V, GND → GND. ENA jumper on L298N left in place (always HIGH).

## Configuration

Edit `src/main.cpp`:

- **Schedule**: modify the `FEEDING_TIMES` array
- **Portion range**: adjust `MIN_DURATION` / `MAX_DURATION` (ms)
- **Set exact time**: uncomment `rtc.SetDateTime(...)` in `setup()`

## Dependencies

- [RTC by Makuna](https://github.com/Makuna/Rtc) (v2.5.0+)

### PlatformIO

```ini
lib_deps = makuna/RTC@^2.5.0
```

### Arduino IDE

Install *RTC by Makuna* via Library Manager.
