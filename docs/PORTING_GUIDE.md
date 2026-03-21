# Porting Guide

Two files: `mboot.h` + `mboot.c`. C99. Provide read/write callbacks for persistent storage.

Storage options: RTC backup registers (STM32), NVS (ESP32), EEPROM, dedicated flash sector.
Only 32 bytes needed.

```cmake
add_library(microboot STATIC lib/microboot/src/mboot.c)
target_include_directories(microboot PUBLIC lib/microboot/include)
```
