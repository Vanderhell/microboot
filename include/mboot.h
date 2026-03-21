/*
 * microboot — Boot & recovery manager for embedded systems.
 *
 * Orchestrates startup: detect boot reason, check for crash dumps,
 * decide boot mode, detect crash loops. Bridges panicdump, nvlog,
 * and microconf.
 *
 * C99 · Zero dependencies · Zero allocations · Callback-driven · Portable
 *
 * SPDX-License-Identifier: MIT
 * https://github.com/Vanderhell/microboot
 */

#ifndef MBOOT_H
#define MBOOT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ── Configuration ─────────────────────────────────────────────────────── */

/** Magic value stored in persistent memory to validate boot record. */
#ifndef MBOOT_MAGIC
#define MBOOT_MAGIC 0x424F4F54U  /* "BOOT" */
#endif

/** Maximum crash loop count before forcing recovery mode. */
#ifndef MBOOT_CRASH_LOOP_THRESHOLD
#define MBOOT_CRASH_LOOP_THRESHOLD 3
#endif

/** Time window (ms) for crash loop detection. Crashes within this window
 *  after boot increment the loop counter. */
#ifndef MBOOT_CRASH_WINDOW_MS
#define MBOOT_CRASH_WINDOW_MS 30000
#endif

/* ── Error codes ───────────────────────────────────────────────────────── */

typedef enum {
    MBOOT_OK            =  0,
    MBOOT_ERR_NULL      = -1,
    MBOOT_ERR_IO        = -2,
    MBOOT_ERR_INVALID   = -3,
    MBOOT_ERR_CORRUPT   = -4,
} mboot_err_t;

const char *mboot_err_str(mboot_err_t err);

/* ── Boot reason ───────────────────────────────────────────────────────── */

typedef enum {
    MBOOT_REASON_COLD       = 0,   /**< First boot / power-on.            */
    MBOOT_REASON_NORMAL     = 1,   /**< Clean reboot (software-initiated).*/
    MBOOT_REASON_WATCHDOG   = 2,   /**< Hardware or software WDT reset.   */
    MBOOT_REASON_CRASH      = 3,   /**< Crash dump found from prev run.   */
    MBOOT_REASON_BROWNOUT   = 4,   /**< Power dip / brownout detected.    */
    MBOOT_REASON_USER       = 5,   /**< User-initiated (button, command). */
} mboot_reason_t;

const char *mboot_reason_str(mboot_reason_t reason);

/* ── Boot mode (decision) ──────────────────────────────────────────────── */

typedef enum {
    MBOOT_MODE_NORMAL      = 0,   /**< Normal startup.                    */
    MBOOT_MODE_RECOVERY    = 1,   /**< Recovery mode (crash loop detected).*/
    MBOOT_MODE_SAFE        = 2,   /**< Safe mode (defaults, minimal).     */
    MBOOT_MODE_FACTORY     = 3,   /**< Factory reset (wipe config+logs).  */
} mboot_mode_t;

const char *mboot_mode_str(mboot_mode_t mode);

/* ── Boot record (persisted in flash/RTC memory) ───────────────────────── */

/**
 * Persistent boot record — survives resets. Store in RTC backup RAM,
 * EEPROM, or a dedicated flash sector.
 *
 * Total: 32 bytes, naturally aligned.
 */
typedef struct {
    uint32_t  magic;              /**< MBOOT_MAGIC for validation.         */
    uint32_t  boot_count;         /**< Total boots since factory.          */
    uint32_t  crash_count;        /**< Consecutive crashes (loop detect).  */
    uint32_t  last_boot_ms;       /**< Uptime at last boot (for window).   */
    uint32_t  last_uptime_ms;     /**< Uptime before last shutdown/crash.  */
    uint8_t   last_reason;        /**< mboot_reason_t of previous boot.    */
    uint8_t   last_mode;          /**< mboot_mode_t of previous boot.      */
    uint8_t   clean_shutdown;     /**< Was last shutdown clean?            */
    uint8_t   _reserved;
    uint32_t  crc32;              /**< CRC of bytes 0..27.                 */
} mboot_record_t;

/* ── Platform callbacks ────────────────────────────────────────────────── */

/** Clock function — returns uptime in milliseconds. */
typedef uint32_t (*mboot_clock_fn)(void);

/**
 * Read boot record from persistent storage.
 * @return 0 on success, negative on failure.
 */
typedef int (*mboot_read_fn)(mboot_record_t *record, void *ctx);

/**
 * Write boot record to persistent storage.
 * @return 0 on success, negative on failure.
 */
typedef int (*mboot_write_fn)(const mboot_record_t *record, void *ctx);

/**
 * Check if a crash dump exists from the previous run.
 * @return true if crash data is available.
 */
typedef bool (*mboot_has_crash_fn)(void *ctx);

/**
 * Detect boot reason from hardware registers.
 *
 * Platform-specific. Read RCC reset flags (STM32), rtc_get_reset_reason
 * (ESP32), etc. Return MBOOT_REASON_COLD if unknown.
 */
typedef mboot_reason_t (*mboot_detect_reason_fn)(void *ctx);

/** Platform I/O bundle. */
typedef struct {
    mboot_read_fn          read_record;
    mboot_write_fn         write_record;
    mboot_has_crash_fn     has_crash;       /**< May be NULL.             */
    mboot_detect_reason_fn detect_reason;   /**< May be NULL (→ COLD).    */
    void                  *io_ctx;          /**< Context for all callbacks.*/
} mboot_io_t;

/* ── Boot decision callback ────────────────────────────────────────────── */

/**
 * Boot decision info — passed to the user's decision callback.
 */
typedef struct {
    mboot_reason_t    reason;          /**< Detected boot reason.          */
    bool              has_crash_dump;  /**< Crash data from prev run?      */
    bool              clean_shutdown;  /**< Was last shutdown clean?        */
    uint32_t          boot_count;      /**< Total boot count.              */
    uint32_t          crash_count;     /**< Consecutive crash count.       */
    uint32_t          last_uptime_ms;  /**< How long the prev run lasted.  */
    bool              crash_loop;      /**< crash_count >= threshold?       */
} mboot_info_t;

/**
 * Boot mode decision callback.
 *
 * Called during mboot_start() with all available info. Return the desired
 * boot mode. If NULL, microboot uses default logic:
 *   - crash_loop → MBOOT_MODE_RECOVERY
 *   - crash_dump → MBOOT_MODE_NORMAL (but info available for logging)
 *   - otherwise  → MBOOT_MODE_NORMAL
 */
typedef mboot_mode_t (*mboot_decide_fn)(const mboot_info_t *info, void *ctx);

/* ── Boot manager instance ─────────────────────────────────────────────── */

typedef struct {
    const mboot_io_t  *io;
    mboot_clock_fn     clock;
    mboot_decide_fn    decide_fn;
    void              *decide_ctx;

    mboot_record_t     record;         /**< Current boot record.           */
    mboot_info_t       info;           /**< Info from current boot.        */
    mboot_mode_t       mode;           /**< Decided boot mode.             */
    bool               started;        /**< Has mboot_start() been called? */
} mboot_t;

/* ── Core API ──────────────────────────────────────────────────────────── */

/**
 * Initialise boot manager.
 *
 * @param boot   Instance (caller-allocated).
 * @param io     Platform I/O callbacks.
 * @param clock  Clock function.
 * @return MBOOT_OK on success.
 */
mboot_err_t mboot_init(mboot_t *boot, const mboot_io_t *io,
                         mboot_clock_fn clock);

/**
 * Set the boot mode decision callback.
 * If not set, default logic is used.
 */
void mboot_set_decide(mboot_t *boot, mboot_decide_fn fn, void *ctx);

/**
 * Execute boot sequence. Call once at startup.
 *
 * 1. Read persistent boot record (validate magic + CRC)
 * 2. Detect boot reason (hardware + crash dump check)
 * 3. Update crash loop counter
 * 4. Call decision callback (or default logic)
 * 5. Write updated boot record
 *
 * After this, query mboot_mode(), mboot_reason(), mboot_info().
 *
 * @return MBOOT_OK on success (even if record was invalid — defaults used).
 */
mboot_err_t mboot_start(mboot_t *boot);

/**
 * Mark a clean shutdown. Call before intentional reboot/poweroff.
 *
 * Sets clean_shutdown flag and writes record. On next boot, microboot
 * knows it wasn't a crash.
 */
mboot_err_t mboot_shutdown(mboot_t *boot);

/**
 * Confirm boot successful. Call after your app is fully initialized.
 *
 * Resets the crash loop counter. If you never call this and the device
 * crashes again, the crash counter keeps incrementing.
 */
mboot_err_t mboot_confirm(mboot_t *boot);

/* ── Query ─────────────────────────────────────────────────────────────── */

/** Get the decided boot mode. */
mboot_mode_t mboot_mode(const mboot_t *boot);

/** Get the detected boot reason. */
mboot_reason_t mboot_reason(const mboot_t *boot);

/** Get full boot info (reason, crash dump, counts, etc.). */
mboot_err_t mboot_get_info(const mboot_t *boot, mboot_info_t *info);

/** Get total boot count. */
uint32_t mboot_boot_count(const mboot_t *boot);

/** Get consecutive crash count. */
uint32_t mboot_crash_count(const mboot_t *boot);

/** Is the device in a crash loop? */
bool mboot_is_crash_loop(const mboot_t *boot);

/* ── CRC utility ───────────────────────────────────────────────────────── */

/** CRC32 (same algorithm as microconf). */
uint32_t mboot_crc32(const void *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* MBOOT_H */
