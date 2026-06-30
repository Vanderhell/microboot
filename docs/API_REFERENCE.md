# API Reference

> **Header:** `#include "mboot.h"` · **Standard:** C99 / C11-compatible headers

## Overview

microboot is a small boot-state manager. It records boot history, detects crash loops, and reports an advisory boot mode. It does not jump to firmware, verify images, perform OTA, or run recovery/factory actions on its own.

## Initialization

1. Create a `mboot_config_t` value.
2. Copy the callback bundle into `mboot_init()`.
3. Validate `crash_loop_threshold > 0`.
4. Move the instance to `READY`.

`mboot_init()` fails with `MBOOT_ERR_NULL` for missing pointers and `MBOOT_ERR_INVALID` for an invalid threshold.

## State machine

- `MBOOT_STATE_UNINITIALIZED`: before `mboot_init()`
- `MBOOT_STATE_READY`: after successful init
- `MBOOT_STATE_STARTED_UNCONFIRMED`: successful start, not yet confirmed
- `MBOOT_STATE_STARTED_CONFIRMED`: confirmed boot
- `MBOOT_STATE_SHUTDOWN_RECORDED`: clean shutdown recorded
- `MBOOT_STATE_ERROR`: a failed mutating operation

Legal transitions:

1. `mboot_init()` -> `READY`
2. `mboot_start()` only from `READY`
3. `mboot_confirm()` only from `STARTED_UNCONFIRMED`
4. `mboot_shutdown()` only after a successful start
5. `mboot_get_info()` only after a successful start
6. `mboot_set_decide()` only from `READY`

Rejected mutating calls return `MBOOT_ERR_STATE` and perform no storage I/O.

## Runtime configuration

`mboot_default_config()` returns the default runtime configuration. The only current policy knob is `crash_loop_threshold`.

## Storage callbacks

microboot uses two logical slots. The storage backend owns erase/program behavior and must distinguish:

- `MBOOT_SLOT_IO_OK`
- `MBOOT_SLOT_IO_EMPTY`
- `MBOOT_SLOT_IO_ERROR`

The library reads both slots independently, selects the newest valid record by generation, and verifies writes by reading the written slot back.

## Wire format

The persisted record is a canonical 32-byte little-endian wire format. It is not the same thing as the public logical structs. The wire format includes:

- fixed magic
- format version
- flags
- generation
- boot count
- consecutive unconfirmed count
- last valid clean uptime
- last reason
- last mode
- CRC32

Reserved bytes must be zero. Unknown versions, invalid enum values, bad CRC, and malformed reserved bytes are rejected.

## Crash-loop semantics

Crash-loop counting is based on the previous persisted flags:

- started
- confirmed
- clean shutdown

Only a prior started, unconfirmed, unclean boot increments the crash counter. Confirmed boots and clean shutdowns do not contribute. The counter saturates at `UINT32_MAX`.

## Crash-dump semantics

Crash-dump evidence is diagnostic. It is not the only crash-loop input and does not override a confirmed or clean shutdown state unless the current persisted state indicates an unconfirmed, unclean prior boot.

## Modes

`mboot_mode()` reports an advisory mode only:

- `NORMAL`
- `RECOVERY`
- `SAFE`
- `FACTORY`

The library does not implement the actions behind those modes.

## Uptime

`last_uptime_valid` tells you whether `last_uptime_ms` is meaningful. Clean shutdown records the uptime using unsigned subtraction, so wraparound is well-defined.

## CRC

`mboot_crc32(NULL, 0)` is valid and returns zero. CRC32 is for accidental corruption detection, not authenticity.

## Threading and callbacks

microboot is not thread-safe for a single instance. Callers must serialize access. Callback contexts are borrowed; they must outlive all calls. The library does not allocate memory or manage cleanup for the callbacks or their contexts.

## Reset API

`mboot_reset_history()` is the explicit caller-approved history-destruction API. It clears both slots using the storage backend.
