# microboot

[![CI](https://github.com/Vanderhell/microboot/actions/workflows/ci.yml/badge.svg?branch=master)](https://github.com/Vanderhell/microboot/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![C99](https://img.shields.io/badge/standard-C99-blue.svg)](https://en.wikipedia.org/wiki/C99)

Boot and recovery manager for embedded systems.

C99 | Zero dependencies | Zero allocations | Callback-driven | Portable

## Why microboot

Without a startup orchestrator, devices can get stuck in crash loops:
boot, fail, reset, repeat.

microboot tracks boot history, detects crash loops, and lets you switch into a recovery strategy before a device gets stuck permanently.

## Features

- Boot reason detection (cold, watchdog, crash dump, brownout, user)
- Crash loop detection with configurable threshold
- Boot mode decision (NORMAL, RECOVERY, SAFE, FACTORY)
- Persistent CRC-protected boot record
- Clean shutdown tracking
- Boot confirmation hook to reset crash counter
- Last uptime tracking for diagnostics

## Quick start

```c
mboot_t boot;
mboot_init(&boot, &flash_io, HAL_GetTick);
mboot_start(&boot);

switch (mboot_mode(&boot)) {
case MBOOT_MODE_NORMAL:
    start_application();
    mboot_confirm(&boot);
    break;
case MBOOT_MODE_RECOVERY:
    start_recovery_server();
    break;
case MBOOT_MODE_FACTORY:
    load_factory_defaults();
    start_application();
    break;
default:
    break;
}
```

## Build and test

Linux/macOS:

```bash
cd tests
make
```

Windows (MSYS2/MinGW shell):

```bash
cd tests
make
```

## Configuration

| Macro | Default | Description |
|-------|---------|-------------|
| `MBOOT_CRASH_LOOP_THRESHOLD` | 3 | Crashes before recovery mode |
| `MBOOT_CRASH_WINDOW_MS` | 30000 | Time window for crash counting |
| `MBOOT_MAGIC` | `0x424F4F54` | Record validation magic |

## Repository structure

- `include/mboot.h`: public API
- `src/mboot.c`: implementation
- `tests/test_all.c`: test suite
- `docs/`: design and integration docs

## Documentation

- [API reference](docs/API_REFERENCE.md)
- [Design notes](docs/DESIGN.md)
- [Porting guide](docs/PORTING_GUIDE.md)
- [Contributing](CONTRIBUTING.md)
- [Changelog](CHANGELOG.md)

## CI

GitHub Actions workflow runs build and tests on each push and pull request to `master`.

## License

MIT License, Copyright (c) 2026 Vanderhell.
See [LICENSE](LICENSE).
