# microboot

[![CI](https://github.com/Vanderhell/microboot/actions/workflows/ci.yml/badge.svg?branch=master)](https://github.com/Vanderhell/microboot/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![C99](https://img.shields.io/badge/standard-C99-blue.svg)](https://en.wikipedia.org/wiki/C99)

Boot and recovery manager for embedded systems.

C99 | C11-compatible headers | Zero heap allocation | Callback-driven | Portable

## Why microboot

Without a startup orchestrator, devices can get stuck in crash loops:
boot, fail, reset, repeat.

microboot tracks boot history, detects crash loops from persisted boot flags, and lets you choose an advisory boot mode before a device gets stuck permanently.

## Features

- Runtime configuration with a crash-loop threshold
- Two-slot persistent storage with read-back verification
- Canonical 32-byte little-endian wire format
- Boot reason detection (cold, normal, watchdog, crash, brownout, user)
- Crash loop detection from unconfirmed, unclean prior boots
- Boot mode decision callback
- Clean shutdown tracking and uptime recording
- Explicit reset API for caller-approved history destruction

microboot chooses and reports a mode. It does not jump to firmware, verify firmware images, implement secure boot, perform OTA, or run recovery/factory actions on its own.

## Quick start

```c
static uint32_t clock_now(void) { return HAL_GetTick(); }

mboot_config_t config = mboot_default_config();
mboot_t boot;
mboot_err_t err;

err = mboot_init(&boot, &flash_io, clock_now, &config);
if (err != MBOOT_OK) {
    return;
}

err = mboot_start(&boot);
if (err != MBOOT_OK) {
    return;
}

switch (mboot_mode(&boot)) {
case MBOOT_MODE_NORMAL:
    start_application();
    err = mboot_confirm(&boot);
    if (err != MBOOT_OK) {
        return;
    }
    break;
case MBOOT_MODE_RECOVERY:
    start_recovery_server();
    break;
case MBOOT_MODE_FACTORY:
    break;
default:
    break;
}

err = mboot_shutdown(&boot);
if (err != MBOOT_OK) {
    return;
}
```

## Build and test

Recommended CMake flow:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The legacy `tests/Makefile` is still available and honors `CC`, `CPPFLAGS`, `CFLAGS`, `LDFLAGS`, and `LDLIBS`.

## Contracts

| Item | Contract |
|------|----------|
| Initialization | `mboot_init()` copies config and callbacks by value, validates the crash-loop threshold, and moves the instance to `READY`. |
| State machine | `mboot_start()` is legal only from `READY`; `confirm()` and `shutdown()` are legal only after a successful start; `get_info()` is valid only after a successful start. |
| Storage | Two logical slots are read independently; empty, corrupt, and I/O errors are reported separately. |
| Wire format | The persisted record is a 32-byte little-endian wire format, not the native public structs. |
| Modes | `NORMAL`, `RECOVERY`, `SAFE`, and `FACTORY` are advisory only. |
| Threading | One instance is not thread-safe; callers must serialize access. Different instances are independent only when their callbacks, contexts, and storage are independent. |
| Cleanup | There is no automatic cleanup, rollback, or deferred resource release. |
| CRC | CRC32 is for accidental corruption detection, not authenticity. |

## Repository structure

- `include/mboot.h`: public API
- `src/mboot.c`: implementation
- `tests/test_all.c`: runtime and negative test suite
- `tests/readme_example.c`: README example build check
- `tests/multi_tu_*.c`: multiple translation unit build check
- `tests/cpp_consumer.cpp`: C++ consumer check
- `cmake/microbootConfig.cmake.in`: package config template
- `docs/`: design and integration docs

## Documentation

- [API reference](docs/API_REFERENCE.md)
- [Design notes](docs/DESIGN.md)
- [Porting guide](docs/PORTING_GUIDE.md)
- [Contributing](CONTRIBUTING.md)
- [Changelog](CHANGELOG.md)

## CI

GitHub Actions workflow runs strict GCC and Clang builds, sanitizers where available, static analysis, install/consumer checks, Windows MSVC, and ARM compile-only coverage.

## License

MIT License, Copyright (c) 2026 Vanderhell.
See [LICENSE](LICENSE).
