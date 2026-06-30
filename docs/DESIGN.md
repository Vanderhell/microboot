# Design Rationale

## 1. Runtime configuration

`crash_loop_threshold` is runtime state because the threshold is a policy choice, not a wire-format detail. The config is copied into `mboot_t` so later caller mutations do not affect an initialized instance.

## 2. Two-slot persistence

microboot reads two logical slots independently. It selects the newest valid generation, tolerates one empty or corrupt slot, and refuses to overwrite the newest valid copy before a replacement write is read back successfully.

## 3. Canonical wire format

The wire format is 32 bytes, fixed-width, and little-endian. The public logical structs are not used as serialized storage because native struct layout and padding would make persistence non-portable.

## 4. Crash-loop counting

Crash-loop counting is derived from the previous record flags, not from a time window. That keeps behavior deterministic across resets without requiring a persistent wall clock.

## 5. Confirm and shutdown

`mboot_confirm()` means the boot made it through initialization. `mboot_shutdown()` means the current run ended cleanly and the recorded uptime is valid. Both write a new candidate record, verify it, and only then update the committed in-memory snapshot.

## 6. Advisory modes

The mode decision is advisory. microboot reports `NORMAL`, `RECOVERY`, `SAFE`, or `FACTORY`, but it does not execute the downstream actions for those modes.

## 7. Callback ownership

The callback bundle is copied by value. The library never owns the callbacks, their context, storage slots, files, mutexes, or peripherals.

## 8. Limits

microboot is not thread-safe for one instance. It does not provide cleanup/defer semantics. It does not survive `abort()`, `_Exit()`, hard faults, power loss, or reset unless the storage backend preserves the last verified slot.

| Decision | Benefit | Cost |
|----------|---------|------|
| Two-slot verified writes | Preserves a good copy across partial writes | Extra storage and a read-back step |
| Runtime threshold | Portable policy control | One more config value |
| Advisory modes | Full caller control | Library stays intentionally small |
| No cleanup automation | No hidden side effects | Caller must manage its own resources |
