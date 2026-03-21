# Changelog

## [1.0.0] — 2026-03-20

### Added
- Boot reason detection (cold, watchdog, crash, brownout, user).
- Crash loop detection with configurable threshold.
- Boot mode decision (NORMAL, RECOVERY, SAFE, FACTORY).
- Persistent CRC-protected boot record (32 bytes).
- Clean shutdown tracking.
- Boot confirmation (proven boot pattern).
- Uptime recording.
- Custom decision callback.
- 32 tests covering full lifecycle, crash loops, IO failures.
