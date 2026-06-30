# Changelog

## [Unreleased]

### Changed
- Replaced the native persisted record with a canonical 32-byte two-slot wire format.
- Added runtime configuration for crash-loop threshold selection.
- Added explicit lifecycle states and rejected invalid transitions.
- Added read-back verification for slot writes.
- Added install/package consumption, C++ consumer coverage, and compile-fail tests.
- Corrected public docs to reflect advisory modes, no firmware jump, and no OTA/recovery execution.
