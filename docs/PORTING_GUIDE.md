# Porting Guide

## Files

microboot ships as `include/mboot.h` plus `src/mboot.c`.

## Storage backend

Implement the two-slot callbacks:

- `mboot_read_slot_fn`
- `mboot_write_slot_fn`

Return:

- `MBOOT_SLOT_IO_OK`
- `MBOOT_SLOT_IO_EMPTY`
- `MBOOT_SLOT_IO_ERROR`

The backend owns erase/program details. A slot may be empty, valid, corrupt, or unreadable; microboot distinguishes those outcomes.

## Clock and reason hooks

`mboot_clock_fn` returns a millisecond tick counter. `mboot_detect_reason_fn` reports the current boot reason. `mboot_has_crash_fn` reports whether crash evidence exists and can fail with `MBOOT_ERR_IO`.

## Example CMake usage

```cmake
add_library(microboot STATIC src/mboot.c)
target_include_directories(microboot PUBLIC include)
target_compile_features(microboot PUBLIC c_std_99)
target_link_libraries(your_app PRIVATE microboot)
```

## Package consumption

The project installs a versionless CMake package config. Consumers should use:

```cmake
find_package(microboot CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE microboot::microboot)
```
