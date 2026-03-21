# Design Rationale

## 1. Persistent boot record with CRC
32 bytes in flash/RTC RAM. CRC protects against corruption. If invalid → defaults (first boot behavior). Simple and robust.

## 2. Crash = "no clean shutdown"
If the record exists and clean_shutdown=0, the previous run crashed. Simple boolean, no complex state tracking. HW reset flags (watchdog, brownout) override this.

## 3. Confirm pattern
App calls mboot_confirm() after successful init. If it crashes before confirm, crash counter keeps incrementing. This is the "proven boot" pattern used by MCUboot and Android's A/B system.

## 4. Custom decision callback
Default logic: crash_loop → RECOVERY, else → NORMAL. But users can override with any logic: check GPIO for factory reset button, check firmware version, etc.

## 5. No hard dependencies
microboot calls panicdump/nvlog/microconf through user callbacks. It never includes their headers.

| Decision | Gains | Costs |
|----------|-------|-------|
| CRC record | Detects corruption | 32 bytes persistent storage |
| clean_shutdown flag | Simple crash detection | Must call shutdown() |
| Confirm pattern | Proven boot guarantee | Must call confirm() |
| Custom decide | Full user control | One more callback |
