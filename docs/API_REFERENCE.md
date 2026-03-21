# API Reference

> **Header:** `#include "mboot.h"` · **Version:** 1.0.0

## Boot sequence: `mboot_start()`
1. Read persistent record → validate magic + CRC
2. Detect reason → HW flags + crash dump check
3. Update crash counter → consecutive unclean boots
4. Decide mode → callback or default (crash_loop → RECOVERY)
5. Write updated record

## Key calls
- `mboot_start()` — run once at boot
- `mboot_confirm()` — "I survived startup" (resets crash counter)
- `mboot_shutdown()` — "I'm shutting down cleanly"

## Thread safety
Single-threaded. Call at startup before any tasks.
