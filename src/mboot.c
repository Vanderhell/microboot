/*
 * microboot — Implementation.
 *
 * SPDX-License-Identifier: MIT
 * https://github.com/Vanderhell/microboot
 */

#include "mboot.h"
#include <string.h>

/* ── Strings ───────────────────────────────────────────────────────────── */

const char *mboot_err_str(mboot_err_t err)
{
    switch (err) {
    case MBOOT_OK:          return "ok";
    case MBOOT_ERR_NULL:    return "null pointer";
    case MBOOT_ERR_IO:      return "io error";
    case MBOOT_ERR_INVALID: return "invalid config";
    case MBOOT_ERR_CORRUPT: return "record corrupt";
    default:                return "unknown error";
    }
}

const char *mboot_reason_str(mboot_reason_t reason)
{
    switch (reason) {
    case MBOOT_REASON_COLD:     return "COLD";
    case MBOOT_REASON_NORMAL:   return "NORMAL";
    case MBOOT_REASON_WATCHDOG: return "WATCHDOG";
    case MBOOT_REASON_CRASH:    return "CRASH";
    case MBOOT_REASON_BROWNOUT: return "BROWNOUT";
    case MBOOT_REASON_USER:     return "USER";
    default:                    return "?";
    }
}

const char *mboot_mode_str(mboot_mode_t mode)
{
    switch (mode) {
    case MBOOT_MODE_NORMAL:   return "NORMAL";
    case MBOOT_MODE_RECOVERY: return "RECOVERY";
    case MBOOT_MODE_SAFE:     return "SAFE";
    case MBOOT_MODE_FACTORY:  return "FACTORY";
    default:                  return "?";
    }
}

/* ── CRC32 (same as microconf — bitwise, no table) ────────────────────── */

uint32_t mboot_crc32(const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFU;

    for (uint32_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 1U) {
                crc = (crc >> 1) ^ 0xEDB88320U;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

/* ── Internal: record CRC ──────────────────────────────────────────────── */

/** CRC covers all fields except the crc32 field itself (last 4 bytes). */
static uint32_t record_crc(const mboot_record_t *rec)
{
    return mboot_crc32(rec, sizeof(*rec) - sizeof(rec->crc32));
}

static bool record_valid(const mboot_record_t *rec)
{
    if (rec->magic != MBOOT_MAGIC) return false;
    return rec->crc32 == record_crc(rec);
}

static void record_update_crc(mboot_record_t *rec)
{
    rec->crc32 = record_crc(rec);
}

static void record_defaults(mboot_record_t *rec)
{
    memset(rec, 0, sizeof(*rec));
    rec->magic = MBOOT_MAGIC;
    record_update_crc(rec);
}

/* ── Internal: default decision logic ──────────────────────────────────── */

static mboot_mode_t default_decide(const mboot_info_t *info)
{
    if (info->crash_loop) {
        return MBOOT_MODE_RECOVERY;
    }
    return MBOOT_MODE_NORMAL;
}

/* ── Init ──────────────────────────────────────────────────────────────── */

mboot_err_t mboot_init(mboot_t *boot, const mboot_io_t *io,
                         mboot_clock_fn clock)
{
    if (boot == NULL || io == NULL || clock == NULL) return MBOOT_ERR_NULL;
    if (io->read_record == NULL || io->write_record == NULL) return MBOOT_ERR_NULL;

    memset(boot, 0, sizeof(*boot));
    boot->io    = io;
    boot->clock = clock;
    boot->mode  = MBOOT_MODE_NORMAL;

    return MBOOT_OK;
}

void mboot_set_decide(mboot_t *boot, mboot_decide_fn fn, void *ctx)
{
    if (boot == NULL) return;
    boot->decide_fn  = fn;
    boot->decide_ctx = ctx;
}

/* ── Start (main boot sequence) ────────────────────────────────────────── */

mboot_err_t mboot_start(mboot_t *boot)
{
    if (boot == NULL) return MBOOT_ERR_NULL;

    const mboot_io_t *io = boot->io;
    mboot_record_t *rec = &boot->record;

    /* 1. Read persistent record */
    bool record_ok = false;
    if (io->read_record(rec, io->io_ctx) == 0) {
        if (record_valid(rec)) {
            record_ok = true;
        }
    }

    if (!record_ok) {
        record_defaults(rec);
    }

    /* 2. Detect boot reason */
    mboot_reason_t reason = MBOOT_REASON_COLD;

    if (io->detect_reason != NULL) {
        reason = io->detect_reason(io->io_ctx);
    }

    /* Check for crash dump — overrides reason to CRASH */
    bool has_crash = false;
    if (io->has_crash != NULL) {
        has_crash = io->has_crash(io->io_ctx);
        if (has_crash && reason == MBOOT_REASON_COLD) {
            reason = MBOOT_REASON_CRASH;
        }
    }

    /* 3. Update crash loop counter */
    if (reason == MBOOT_REASON_CRASH || reason == MBOOT_REASON_WATCHDOG) {
        /* HW or crash dump says this was a crash — always count */
        rec->crash_count++;
    } else if (!rec->clean_shutdown && record_ok) {
        /* No clean shutdown on a valid record → likely crash */
        rec->crash_count++;
    } else if (rec->clean_shutdown) {
        /* Clean shutdown → reset crash counter */
        rec->crash_count = 0;
    }

    bool crash_loop = (rec->crash_count >= MBOOT_CRASH_LOOP_THRESHOLD);

    /* 4. Build info struct */
    mboot_info_t *info = &boot->info;
    info->reason          = reason;
    info->has_crash_dump  = has_crash;
    info->clean_shutdown  = rec->clean_shutdown;
    info->boot_count      = rec->boot_count + 1;
    info->crash_count     = rec->crash_count;
    info->last_uptime_ms  = rec->last_uptime_ms;
    info->crash_loop      = crash_loop;

    /* 5. Decide boot mode */
    mboot_mode_t mode;
    if (boot->decide_fn != NULL) {
        mode = boot->decide_fn(info, boot->decide_ctx);
    } else {
        mode = default_decide(info);
    }
    boot->mode = mode;

    /* 6. Update and write record for this boot */
    rec->boot_count    = info->boot_count;
    rec->last_reason   = (uint8_t)reason;
    rec->last_mode     = (uint8_t)mode;
    rec->clean_shutdown = 0;  /* will be set by mboot_shutdown() */
    rec->last_boot_ms  = boot->clock();
    record_update_crc(rec);

    if (io->write_record(rec, io->io_ctx) != 0) {
        return MBOOT_ERR_IO;
    }

    boot->started = true;
    return MBOOT_OK;
}

/* ── Shutdown (mark clean exit) ────────────────────────────────────────── */

mboot_err_t mboot_shutdown(mboot_t *boot)
{
    if (boot == NULL) return MBOOT_ERR_NULL;

    mboot_record_t *rec = &boot->record;
    rec->clean_shutdown = 1;
    rec->last_uptime_ms = boot->clock() - rec->last_boot_ms;
    record_update_crc(rec);

    if (boot->io->write_record(rec, boot->io->io_ctx) != 0) {
        return MBOOT_ERR_IO;
    }

    return MBOOT_OK;
}

/* ── Confirm (reset crash loop) ────────────────────────────────────────── */

mboot_err_t mboot_confirm(mboot_t *boot)
{
    if (boot == NULL) return MBOOT_ERR_NULL;

    mboot_record_t *rec = &boot->record;
    rec->crash_count = 0;
    record_update_crc(rec);

    if (boot->io->write_record(rec, boot->io->io_ctx) != 0) {
        return MBOOT_ERR_IO;
    }

    return MBOOT_OK;
}

/* ── Query ─────────────────────────────────────────────────────────────── */

mboot_mode_t mboot_mode(const mboot_t *boot)
{
    if (boot == NULL) return MBOOT_MODE_NORMAL;
    return boot->mode;
}

mboot_reason_t mboot_reason(const mboot_t *boot)
{
    if (boot == NULL) return MBOOT_REASON_COLD;
    return boot->info.reason;
}

mboot_err_t mboot_get_info(const mboot_t *boot, mboot_info_t *info)
{
    if (boot == NULL || info == NULL) return MBOOT_ERR_NULL;
    *info = boot->info;
    return MBOOT_OK;
}

uint32_t mboot_boot_count(const mboot_t *boot)
{
    if (boot == NULL) return 0;
    return boot->record.boot_count;
}

uint32_t mboot_crash_count(const mboot_t *boot)
{
    if (boot == NULL) return 0;
    return boot->record.crash_count;
}

bool mboot_is_crash_loop(const mboot_t *boot)
{
    if (boot == NULL) return false;
    return boot->info.crash_loop;
}
