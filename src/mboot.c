/*
 * microboot - implementation.
 *
 * SPDX-License-Identifier: MIT
 * https://github.com/Vanderhell/microboot
 */

#include "mboot.h"

#include <string.h>

enum {
    MBOOT_WIRE_OFF_MAGIC = 0,
    MBOOT_WIRE_OFF_VERSION = 4,
    MBOOT_WIRE_OFF_FLAGS = 5,
    MBOOT_WIRE_OFF_GENERATION = 8,
    MBOOT_WIRE_OFF_BOOT_COUNT = 12,
    MBOOT_WIRE_OFF_CRASH_COUNT = 16,
    MBOOT_WIRE_OFF_LAST_UPTIME_MS = 20,
    MBOOT_WIRE_OFF_LAST_REASON = 24,
    MBOOT_WIRE_OFF_LAST_MODE = 25,
    MBOOT_WIRE_OFF_CRC32 = 28
};

enum {
    MBOOT_WIRE_HEADER_BYTES = 28
};

static const mboot_config_t default_config = { MBOOT_DEFAULT_CRASH_LOOP_THRESHOLD };

static uint32_t read_u32_le(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void write_u32_le(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value & 0xFFu);
    p[1] = (uint8_t)((value >> 8) & 0xFFu);
    p[2] = (uint8_t)((value >> 16) & 0xFFu);
    p[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static void zero_memory(void *ptr, size_t len)
{
    uint8_t *p = (uint8_t *)ptr;
    while (len-- != 0u) {
        *p++ = 0u;
    }
}

static bool reason_valid(mboot_reason_t reason)
{
    switch (reason) {
    case MBOOT_REASON_COLD:
    case MBOOT_REASON_NORMAL:
    case MBOOT_REASON_WATCHDOG:
    case MBOOT_REASON_CRASH:
    case MBOOT_REASON_BROWNOUT:
    case MBOOT_REASON_USER:
        return true;
    default:
        return false;
    }
}

static bool mode_valid(mboot_mode_t mode)
{
    switch (mode) {
    case MBOOT_MODE_NORMAL:
    case MBOOT_MODE_RECOVERY:
    case MBOOT_MODE_SAFE:
    case MBOOT_MODE_FACTORY:
        return true;
    default:
        return false;
    }
}

static uint32_t saturating_inc(uint32_t value)
{
    return value == UINT32_MAX ? UINT32_MAX : value + 1u;
}

static bool generation_newer(uint32_t a, uint32_t b)
{
    return a != b && (int32_t)(a - b) > 0;
}

static uint32_t wire_crc(const mboot_wire_t *wire)
{
    return mboot_crc32(wire->bytes, MBOOT_WIRE_HEADER_BYTES);
}

static void wire_zero(mboot_wire_t *wire)
{
    zero_memory(wire->bytes, sizeof(wire->bytes));
}

static void wire_encode(mboot_wire_t *wire, const mboot_info_t *info,
                        mboot_mode_t mode, uint8_t flags)
{
    wire_zero(wire);
    write_u32_le(&wire->bytes[MBOOT_WIRE_OFF_MAGIC], MBOOT_WIRE_MAGIC);
    wire->bytes[MBOOT_WIRE_OFF_VERSION] = (uint8_t)MBOOT_WIRE_VERSION;
    wire->bytes[MBOOT_WIRE_OFF_FLAGS] = flags;
    write_u32_le(&wire->bytes[MBOOT_WIRE_OFF_GENERATION], info->generation);
    write_u32_le(&wire->bytes[MBOOT_WIRE_OFF_BOOT_COUNT], info->boot_count);
    write_u32_le(&wire->bytes[MBOOT_WIRE_OFF_CRASH_COUNT], info->crash_count);
    write_u32_le(&wire->bytes[MBOOT_WIRE_OFF_LAST_UPTIME_MS], info->last_uptime_ms);
    wire->bytes[MBOOT_WIRE_OFF_LAST_REASON] = (uint8_t)info->reason;
    wire->bytes[MBOOT_WIRE_OFF_LAST_MODE] = (uint8_t)mode;
    write_u32_le(&wire->bytes[MBOOT_WIRE_OFF_CRC32], wire_crc(wire));
}

static mboot_slot_io_result_t wire_decode(const mboot_wire_t *wire,
                                          mboot_info_t *info,
                                          mboot_mode_t *mode,
                                          uint8_t *flags_out)
{
    uint8_t flags = wire->bytes[MBOOT_WIRE_OFF_FLAGS];
    uint8_t version = wire->bytes[MBOOT_WIRE_OFF_VERSION];
    uint8_t reserved0 = wire->bytes[6];
    uint8_t reserved1 = wire->bytes[7];
    uint8_t reserved2 = wire->bytes[26];
    uint8_t reserved3 = wire->bytes[27];
    uint32_t crc = read_u32_le(&wire->bytes[MBOOT_WIRE_OFF_CRC32]);

    bool all_zero = true;
    for (size_t i = 0u; i < MBOOT_WIRE_BYTES; ++i) {
        if (wire->bytes[i] != 0u) {
            all_zero = false;
            break;
        }
    }

    if (all_zero) {
        return MBOOT_SLOT_IO_EMPTY;
    }
    if (read_u32_le(&wire->bytes[MBOOT_WIRE_OFF_MAGIC]) != MBOOT_WIRE_MAGIC) {
        return MBOOT_SLOT_IO_ERROR;
    }
    if (version != MBOOT_WIRE_VERSION) {
        return MBOOT_SLOT_IO_ERROR;
    }
    if ((flags & MBOOT_WIRE_FLAG_RESERVED_MASK) != 0u) {
        return MBOOT_SLOT_IO_ERROR;
    }
    if (reserved0 != 0u || reserved1 != 0u || reserved2 != 0u || reserved3 != 0u) {
        return MBOOT_SLOT_IO_ERROR;
    }
    if (crc != wire_crc(wire)) {
        return MBOOT_SLOT_IO_ERROR;
    }

    info->generation = read_u32_le(&wire->bytes[MBOOT_WIRE_OFF_GENERATION]);
    info->boot_count = read_u32_le(&wire->bytes[MBOOT_WIRE_OFF_BOOT_COUNT]);
    info->crash_count = read_u32_le(&wire->bytes[MBOOT_WIRE_OFF_CRASH_COUNT]);
    info->last_uptime_ms = read_u32_le(&wire->bytes[MBOOT_WIRE_OFF_LAST_UPTIME_MS]);
    info->reason = (mboot_reason_t)wire->bytes[MBOOT_WIRE_OFF_LAST_REASON];
    *mode = (mboot_mode_t)wire->bytes[MBOOT_WIRE_OFF_LAST_MODE];
    *flags_out = flags;

    if (!reason_valid(info->reason) || !mode_valid(*mode)) {
        return MBOOT_SLOT_IO_ERROR;
    }
    return MBOOT_SLOT_IO_OK;
}

static mboot_mode_t default_decide(const mboot_info_t *info, void *ctx)
{
    (void)ctx;
    return info->crash_loop ? MBOOT_MODE_RECOVERY : MBOOT_MODE_NORMAL;
}

static mboot_mode_t call_decide(const mboot_t *boot, const mboot_info_t *info)
{
    if (boot->decide_fn != NULL) {
        return boot->decide_fn(info, boot->decide_ctx);
    }
    return default_decide(info, NULL);
}

static mboot_err_t validate_io(const mboot_io_t *io)
{
    if (io == NULL || io->read_slot == NULL || io->write_slot == NULL) {
        return MBOOT_ERR_NULL;
    }
    return MBOOT_OK;
}

static mboot_err_t validate_config(const mboot_config_t *config)
{
    if (config == NULL) {
        return MBOOT_ERR_NULL;
    }
    if (config->crash_loop_threshold == 0u) {
        return MBOOT_ERR_INVALID;
    }
    return MBOOT_OK;
}

static void boot_clear_runtime(mboot_t *boot)
{
    zero_memory(&boot->info, sizeof(boot->info));
    boot->mode = MBOOT_MODE_NORMAL;
    boot->state = MBOOT_STATE_READY;
    boot->has_active_slot = false;
    boot->active_slot = 0u;
    boot->busy = false;
    boot->start_tick = 0u;
    wire_zero(&boot->active_wire);
}

static mboot_err_t read_slots(const mboot_t *boot,
                              mboot_wire_t *slot0,
                              mboot_wire_t *slot1,
                              bool *slot0_empty,
                              bool *slot1_empty,
                              bool *io_error)
{
    mboot_slot_io_result_t r0 = boot->io.read_slot(0u, slot0, boot->io.io_ctx);
    mboot_slot_io_result_t r1 = boot->io.read_slot(1u, slot1, boot->io.io_ctx);

    *slot0_empty = false;
    *slot1_empty = false;
    *io_error = false;

    if (r0 == MBOOT_SLOT_IO_ERROR || r1 == MBOOT_SLOT_IO_ERROR) {
        *io_error = true;
        return MBOOT_ERR_IO;
    }
    if (r0 == MBOOT_SLOT_IO_EMPTY) {
        *slot0_empty = true;
        wire_zero(slot0);
    } else if (r0 != MBOOT_SLOT_IO_OK) {
        *io_error = true;
        return MBOOT_ERR_IO;
    }
    if (r1 == MBOOT_SLOT_IO_EMPTY) {
        *slot1_empty = true;
        wire_zero(slot1);
    } else if (r1 != MBOOT_SLOT_IO_OK) {
        *io_error = true;
        return MBOOT_ERR_IO;
    }
    return MBOOT_OK;
}

static bool wire_equal(const mboot_wire_t *a, const mboot_wire_t *b)
{
    return memcmp(a->bytes, b->bytes, sizeof(a->bytes)) == 0;
}

static mboot_err_t verify_write(mboot_t *boot, uint8_t slot,
                                const mboot_wire_t *candidate)
{
    mboot_wire_t check;

    if (boot->io.write_slot(slot, candidate, boot->io.io_ctx) != MBOOT_SLOT_IO_OK) {
        return MBOOT_ERR_IO;
    }
    if (boot->io.read_slot(slot, &check, boot->io.io_ctx) != MBOOT_SLOT_IO_OK) {
        return MBOOT_ERR_IO;
    }
    if (!wire_equal(candidate, &check)) {
        return MBOOT_ERR_IO;
    }
    return MBOOT_OK;
}

static void commit_from_info(mboot_t *boot, const mboot_info_t *info,
                             mboot_mode_t mode, uint8_t active_slot,
                             const mboot_wire_t *wire, mboot_state_t state)
{
    boot->info = *info;
    boot->mode = mode;
    boot->active_slot = active_slot;
    boot->has_active_slot = true;
    boot->active_wire = *wire;
    boot->state = state;
}

static uint8_t derive_flags(bool started, bool confirmed, bool clean_shutdown,
                            bool last_uptime_valid)
{
    uint8_t flags = 0u;
    if (started) {
        flags |= MBOOT_WIRE_FLAG_STARTED;
    }
    if (confirmed) {
        flags |= MBOOT_WIRE_FLAG_CONFIRMED;
    }
    if (clean_shutdown) {
        flags |= MBOOT_WIRE_FLAG_CLEAN_SHUTDOWN;
    }
    if (last_uptime_valid) {
        flags |= MBOOT_WIRE_FLAG_LAST_UPTIME_VALID;
    }
    return flags;
}

static mboot_err_t build_candidate(const mboot_t *boot,
                                   const mboot_info_t *current,
                                   mboot_reason_t reason,
                                   bool has_crash_dump,
                                   mboot_info_t *next_info,
                                   mboot_mode_t *next_mode,
                                   mboot_wire_t *next_wire)
{
    mboot_info_t info = *current;
    mboot_mode_t mode;

    info.boot_count = saturating_inc(info.boot_count);
    if (current->started && !current->confirmed && !current->clean_shutdown) {
        info.crash_count = saturating_inc(info.crash_count);
    } else {
        info.crash_count = 0u;
    }
    info.reason = reason;
    info.started = true;
    info.confirmed = false;
    info.clean_shutdown = false;
    info.crash_loop = info.crash_count >= boot->config.crash_loop_threshold;
    info.last_uptime_valid = current->last_uptime_valid;
    if (!info.last_uptime_valid) {
        info.last_uptime_ms = 0u;
    }
    info.generation = current->generation + 1u;

    if (has_crash_dump && reason == MBOOT_REASON_COLD && !current->clean_shutdown) {
        info.reason = MBOOT_REASON_CRASH;
    }

    mode = call_decide(boot, &info);
    if (!mode_valid(mode)) {
        return MBOOT_ERR_INVALID;
    }

    wire_encode(next_wire, &info, mode,
                derive_flags(true, false, false, info.last_uptime_valid));
    *next_info = info;
    *next_mode = mode;
    return MBOOT_OK;
}

mboot_err_t mboot_init(mboot_t *boot, const mboot_io_t *io,
                       mboot_clock_fn clock, const mboot_config_t *config)
{
    mboot_err_t io_err;
    mboot_err_t config_err;

    if (boot == NULL || io == NULL || clock == NULL) {
        return MBOOT_ERR_NULL;
    }
    io_err = validate_io(io);
    if (io_err != MBOOT_OK) {
        return io_err;
    }
    config_err = validate_config(config);
    if (config_err != MBOOT_OK) {
        return config_err;
    }

    zero_memory(boot, sizeof(*boot));
    boot->config = *config;
    boot->io = *io;
    boot->clock = clock;
    boot->mode = MBOOT_MODE_NORMAL;
    boot->state = MBOOT_STATE_READY;
    boot->info.last_uptime_valid = false;
    return MBOOT_OK;
}

mboot_config_t mboot_default_config(void)
{
    return default_config;
}

mboot_err_t mboot_set_decide(mboot_t *boot, mboot_decide_fn fn, void *ctx)
{
    if (boot == NULL) {
        return MBOOT_ERR_NULL;
    }
    if (boot->busy) {
        return MBOOT_ERR_STATE;
    }
    if (boot->state != MBOOT_STATE_READY) {
        return MBOOT_ERR_STATE;
    }
    boot->decide_fn = fn;
    boot->decide_ctx = ctx;
    return MBOOT_OK;
}

mboot_err_t mboot_start(mboot_t *boot)
{
    mboot_wire_t slot0;
    mboot_wire_t slot1;
    mboot_info_t info0;
    mboot_info_t info1;
    mboot_info_t current;
    mboot_info_t next_info;
    mboot_mode_t mode = MBOOT_MODE_NORMAL;
    uint8_t flags0 = 0u;
    uint8_t flags1 = 0u;
    uint8_t active_slot = 0u;
    bool slot0_empty = false;
    bool slot1_empty = false;
    bool io_error = false;
    bool valid0 = false;
    bool valid1 = false;
    mboot_reason_t reason;
    bool has_crash = false;
    mboot_err_t err;
    mboot_mode_t decoded_mode0 = MBOOT_MODE_NORMAL;
    mboot_mode_t decoded_mode1 = MBOOT_MODE_NORMAL;

    if (boot == NULL) {
        return MBOOT_ERR_NULL;
    }
    if (boot->state != MBOOT_STATE_READY) {
        return MBOOT_ERR_STATE;
    }

    boot->busy = true;
    zero_memory(&info0, sizeof(info0));
    zero_memory(&info1, sizeof(info1));
    zero_memory(&current, sizeof(current));
    zero_memory(&next_info, sizeof(next_info));

    err = read_slots(boot, &slot0, &slot1, &slot0_empty, &slot1_empty, &io_error);
    if (err != MBOOT_OK || io_error) {
        boot->busy = false;
        boot->state = MBOOT_STATE_ERROR;
        return MBOOT_ERR_IO;
    }

    if (!slot0_empty) {
        if (wire_decode(&slot0, &info0, &decoded_mode0, &flags0) == MBOOT_SLOT_IO_OK) {
            info0.started = (flags0 & MBOOT_WIRE_FLAG_STARTED) != 0u;
            info0.confirmed = (flags0 & MBOOT_WIRE_FLAG_CONFIRMED) != 0u;
            info0.clean_shutdown = (flags0 & MBOOT_WIRE_FLAG_CLEAN_SHUTDOWN) != 0u;
            info0.last_uptime_valid = (flags0 & MBOOT_WIRE_FLAG_LAST_UPTIME_VALID) != 0u;
            info0.crash_loop = info0.crash_count >= boot->config.crash_loop_threshold;
            valid0 = true;
        }
    }
    if (!slot1_empty) {
        if (wire_decode(&slot1, &info1, &decoded_mode1, &flags1) == MBOOT_SLOT_IO_OK) {
            info1.started = (flags1 & MBOOT_WIRE_FLAG_STARTED) != 0u;
            info1.confirmed = (flags1 & MBOOT_WIRE_FLAG_CONFIRMED) != 0u;
            info1.clean_shutdown = (flags1 & MBOOT_WIRE_FLAG_CLEAN_SHUTDOWN) != 0u;
            info1.last_uptime_valid = (flags1 & MBOOT_WIRE_FLAG_LAST_UPTIME_VALID) != 0u;
            info1.crash_loop = info1.crash_count >= boot->config.crash_loop_threshold;
            valid1 = true;
        }
    }

    if (valid0 && valid1) {
        if (info0.generation == info1.generation) {
            if (!wire_equal(&slot0, &slot1)) {
                boot->busy = false;
                boot->state = MBOOT_STATE_ERROR;
                return MBOOT_ERR_CORRUPT;
            }
            active_slot = 0u;
            current = info0;
            mode = decoded_mode0;
        } else if (generation_newer(info1.generation, info0.generation)) {
            active_slot = 1u;
            current = info1;
            mode = decoded_mode1;
        } else {
            active_slot = 0u;
            current = info0;
            mode = decoded_mode0;
        }
    } else if (valid0) {
        active_slot = 0u;
        current = info0;
        mode = decoded_mode0;
    } else if (valid1) {
        active_slot = 1u;
        current = info1;
        mode = decoded_mode1;
    } else if (slot0_empty && slot1_empty) {
        zero_memory(&current, sizeof(current));
        current.generation = 0u;
        current.reason = MBOOT_REASON_COLD;
        current.last_uptime_valid = false;
        current.started = false;
        current.confirmed = false;
        current.clean_shutdown = false;
        current.crash_loop = false;
        current.boot_count = 0u;
        current.crash_count = 0u;
        current.last_uptime_ms = 0u;
        mode = MBOOT_MODE_NORMAL;
    } else {
        boot->busy = false;
        boot->state = MBOOT_STATE_ERROR;
        return MBOOT_ERR_CORRUPT;
    }

    if (boot->io.has_crash != NULL) {
        mboot_err_t crash_err = boot->io.has_crash(&has_crash, boot->io.io_ctx);
        if (crash_err != MBOOT_OK) {
            boot->busy = false;
            boot->state = MBOOT_STATE_ERROR;
            return MBOOT_ERR_IO;
        }
    }

    if (boot->io.detect_reason != NULL) {
        reason = boot->io.detect_reason(boot->io.io_ctx);
        if (!reason_valid(reason)) {
            boot->busy = false;
            boot->state = MBOOT_STATE_ERROR;
            return MBOOT_ERR_INVALID;
        }
    } else {
        reason = MBOOT_REASON_COLD;
    }

    if (valid0 || valid1) {
        if (has_crash && current.clean_shutdown) {
            has_crash = false;
        }
        if (has_crash && !current.confirmed) {
            reason = MBOOT_REASON_CRASH;
        }
    } else if (has_crash) {
        reason = MBOOT_REASON_CRASH;
    }

    err = build_candidate(boot, &current, reason, has_crash, &next_info, &mode, &boot->active_wire);
    if (err != MBOOT_OK) {
        boot->busy = false;
        boot->state = MBOOT_STATE_ERROR;
        return err;
    }

    if (slot0_empty && slot1_empty) {
        active_slot = 0u;
        if (verify_write(boot, active_slot, &boot->active_wire) != MBOOT_OK) {
            boot->busy = false;
            boot->state = MBOOT_STATE_ERROR;
            return MBOOT_ERR_IO;
        }
    } else {
        active_slot = (uint8_t)(1u - active_slot);
        if (verify_write(boot, active_slot, &boot->active_wire) != MBOOT_OK) {
            boot->busy = false;
            boot->state = MBOOT_STATE_ERROR;
            return MBOOT_ERR_IO;
        }
    }

    next_info.last_uptime_valid = current.last_uptime_valid;
    if (current.last_uptime_valid) {
        next_info.last_uptime_ms = current.last_uptime_ms;
    }
    next_info.crash_loop = next_info.crash_count >= boot->config.crash_loop_threshold;
    commit_from_info(boot, &next_info, mode, active_slot, &boot->active_wire,
                     MBOOT_STATE_STARTED_UNCONFIRMED);
    boot->start_tick = boot->clock();
    boot->busy = false;
    return MBOOT_OK;
}

static mboot_err_t update_state_after_write(mboot_t *boot, const mboot_info_t *info,
                                            mboot_mode_t mode, uint8_t active_slot,
                                            const mboot_wire_t *wire,
                                            mboot_state_t state)
{
    commit_from_info(boot, info, mode, active_slot, wire, state);
    return MBOOT_OK;
}

static mboot_err_t prepare_current_update(const mboot_t *boot,
                                          mboot_info_t *next_info,
                                          mboot_mode_t *next_mode,
                                          mboot_wire_t *next_wire,
                                          uint8_t *target_slot)
{
    mboot_info_t current = boot->info;
    mboot_mode_t mode = boot->mode;

    if (boot->state != MBOOT_STATE_STARTED_UNCONFIRMED &&
        boot->state != MBOOT_STATE_STARTED_CONFIRMED &&
        boot->state != MBOOT_STATE_SHUTDOWN_RECORDED) {
        return MBOOT_ERR_STATE;
    }

    current.generation = boot->info.generation + 1u;
    current.boot_count = boot->info.boot_count;
    current.crash_count = boot->info.crash_count;
    current.reason = boot->info.reason;
    current.last_uptime_ms = boot->info.last_uptime_ms;
    current.last_uptime_valid = boot->info.last_uptime_valid;
    current.started = boot->info.started;
    current.confirmed = boot->info.confirmed;
    current.clean_shutdown = boot->info.clean_shutdown;
    current.crash_loop = boot->info.crash_loop;

    if (boot->state == MBOOT_STATE_STARTED_UNCONFIRMED) {
        current.confirmed = true;
    }
    if (boot->state == MBOOT_STATE_SHUTDOWN_RECORDED) {
        current.confirmed = true;
        current.clean_shutdown = true;
    }

    if (next_info != NULL) {
        *next_info = current;
    }
    if (next_mode != NULL) {
        *next_mode = mode;
    }
    if (next_wire != NULL) {
        wire_encode(next_wire, &current, mode,
                    derive_flags(current.started, current.confirmed,
                                 current.clean_shutdown, current.last_uptime_valid));
    }
    if (target_slot != NULL) {
        *target_slot = boot->has_active_slot ? (uint8_t)(1u - boot->active_slot) : 0u;
    }
    return MBOOT_OK;
}

mboot_err_t mboot_confirm(mboot_t *boot)
{
    mboot_info_t next_info;
    mboot_mode_t next_mode;
    mboot_wire_t next_wire;
    uint8_t target_slot;
    mboot_err_t err;

    if (boot == NULL) {
        return MBOOT_ERR_NULL;
    }
    if (boot->busy) {
        return MBOOT_ERR_STATE;
    }
    if (boot->state != MBOOT_STATE_STARTED_UNCONFIRMED) {
        return MBOOT_ERR_STATE;
    }
    boot->busy = true;
    err = prepare_current_update(boot, &next_info, &next_mode, &next_wire, &target_slot);
    if (err != MBOOT_OK) {
        boot->busy = false;
        return err;
    }
    next_info.confirmed = true;
    next_info.clean_shutdown = false;
    next_info.generation = boot->info.generation + 1u;
    next_info.crash_loop = next_info.crash_count >= boot->config.crash_loop_threshold;
    wire_encode(&next_wire, &next_info, next_mode,
                derive_flags(true, true, false, next_info.last_uptime_valid));
    if (verify_write(boot, target_slot, &next_wire) != MBOOT_OK) {
        boot->busy = false;
        return MBOOT_ERR_IO;
    }
    err = update_state_after_write(boot, &next_info, next_mode, target_slot,
                                   &next_wire, MBOOT_STATE_STARTED_CONFIRMED);
    boot->busy = false;
    return err;
}

mboot_err_t mboot_shutdown(mboot_t *boot)
{
    mboot_info_t next_info;
    mboot_mode_t next_mode;
    mboot_wire_t next_wire;
    uint8_t target_slot;
    mboot_err_t err;
    uint32_t uptime;

    if (boot == NULL) {
        return MBOOT_ERR_NULL;
    }
    if (boot->busy) {
        return MBOOT_ERR_STATE;
    }
    if (boot->state != MBOOT_STATE_STARTED_UNCONFIRMED &&
        boot->state != MBOOT_STATE_STARTED_CONFIRMED) {
        return MBOOT_ERR_STATE;
    }
    boot->busy = true;
    err = prepare_current_update(boot, &next_info, &next_mode, &next_wire, &target_slot);
    if (err != MBOOT_OK) {
        boot->busy = false;
        return err;
    }
    uptime = boot->clock() - boot->start_tick;
    next_info.last_uptime_ms = uptime;
    next_info.last_uptime_valid = true;
    next_info.confirmed = true;
    next_info.clean_shutdown = true;
    next_info.generation = boot->info.generation + 1u;
    next_info.crash_count = 0u;
    next_info.crash_loop = false;
    wire_encode(&next_wire, &next_info, next_mode,
                derive_flags(true, true, true, true));
    if (verify_write(boot, target_slot, &next_wire) != MBOOT_OK) {
        boot->busy = false;
        return MBOOT_ERR_IO;
    }
    err = update_state_after_write(boot, &next_info, next_mode, target_slot,
                                   &next_wire, MBOOT_STATE_SHUTDOWN_RECORDED);
    boot->busy = false;
    return err;
}

mboot_err_t mboot_reset_history(mboot_t *boot)
{
    mboot_wire_t empty;
    uint8_t slot;

    if (boot == NULL) {
        return MBOOT_ERR_NULL;
    }
    if (boot->busy) {
        return MBOOT_ERR_STATE;
    }
    if (boot->state == MBOOT_STATE_UNINITIALIZED) {
        return MBOOT_ERR_STATE;
    }

    boot->busy = true;
    wire_zero(&empty);
    if (boot->io.write_slot(0u, &empty, boot->io.io_ctx) != MBOOT_SLOT_IO_OK) {
        boot->busy = false;
        return MBOOT_ERR_IO;
    }
    if (boot->io.write_slot(1u, &empty, boot->io.io_ctx) != MBOOT_SLOT_IO_OK) {
        boot->busy = false;
        return MBOOT_ERR_IO;
    }
    slot = 0u;
    boot_clear_runtime(boot);
    boot->state = MBOOT_STATE_READY;
    boot->has_active_slot = false;
    boot->active_slot = slot;
    boot->busy = false;
    return MBOOT_OK;
}

mboot_state_t mboot_state(const mboot_t *boot)
{
    return boot == NULL ? MBOOT_STATE_UNINITIALIZED : boot->state;
}

mboot_mode_t mboot_mode(const mboot_t *boot)
{
    return boot == NULL ? MBOOT_MODE_NORMAL : boot->mode;
}

mboot_reason_t mboot_reason(const mboot_t *boot)
{
    return boot == NULL ? MBOOT_REASON_COLD : boot->info.reason;
}

mboot_err_t mboot_get_info(const mboot_t *boot, mboot_info_t *info)
{
    if (boot == NULL || info == NULL) {
        return MBOOT_ERR_NULL;
    }
    if (boot->state == MBOOT_STATE_READY || boot->state == MBOOT_STATE_UNINITIALIZED ||
        boot->state == MBOOT_STATE_ERROR) {
        return MBOOT_ERR_STATE;
    }
    *info = boot->info;
    return MBOOT_OK;
}

uint32_t mboot_boot_count(const mboot_t *boot)
{
    return boot == NULL ? 0u : boot->info.boot_count;
}

uint32_t mboot_crash_count(const mboot_t *boot)
{
    return boot == NULL ? 0u : boot->info.crash_count;
}

bool mboot_is_crash_loop(const mboot_t *boot)
{
    return boot == NULL ? false : boot->info.crash_loop;
}

const char *mboot_err_str(mboot_err_t err)
{
    switch (err) {
    case MBOOT_OK: return "ok";
    case MBOOT_ERR_NULL: return "null pointer";
    case MBOOT_ERR_STATE: return "invalid state";
    case MBOOT_ERR_IO: return "io error";
    case MBOOT_ERR_INVALID: return "invalid value";
    case MBOOT_ERR_CORRUPT: return "corrupt record";
    default: return "unknown error";
    }
}

const char *mboot_state_str(mboot_state_t state)
{
    switch (state) {
    case MBOOT_STATE_UNINITIALIZED: return "UNINITIALIZED";
    case MBOOT_STATE_READY: return "READY";
    case MBOOT_STATE_STARTED_UNCONFIRMED: return "STARTED_UNCONFIRMED";
    case MBOOT_STATE_STARTED_CONFIRMED: return "STARTED_CONFIRMED";
    case MBOOT_STATE_SHUTDOWN_RECORDED: return "SHUTDOWN_RECORDED";
    case MBOOT_STATE_ERROR: return "ERROR";
    default: return "?";
    }
}

const char *mboot_reason_str(mboot_reason_t reason)
{
    switch (reason) {
    case MBOOT_REASON_COLD: return "COLD";
    case MBOOT_REASON_NORMAL: return "NORMAL";
    case MBOOT_REASON_WATCHDOG: return "WATCHDOG";
    case MBOOT_REASON_CRASH: return "CRASH";
    case MBOOT_REASON_BROWNOUT: return "BROWNOUT";
    case MBOOT_REASON_USER: return "USER";
    default: return "?";
    }
}

const char *mboot_mode_str(mboot_mode_t mode)
{
    switch (mode) {
    case MBOOT_MODE_NORMAL: return "NORMAL";
    case MBOOT_MODE_RECOVERY: return "RECOVERY";
    case MBOOT_MODE_SAFE: return "SAFE";
    case MBOOT_MODE_FACTORY: return "FACTORY";
    default: return "?";
    }
}

const char *mboot_slot_io_result_str(mboot_slot_io_result_t result)
{
    switch (result) {
    case MBOOT_SLOT_IO_OK: return "OK";
    case MBOOT_SLOT_IO_EMPTY: return "EMPTY";
    case MBOOT_SLOT_IO_ERROR: return "ERROR";
    default: return "?";
    }
}

uint32_t mboot_crc32(const void *data, uint32_t len)
{
    const unsigned char *p = (const unsigned char *)data;
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t i;

    if (len == 0u) {
        return 0u;
    }
    if (p == NULL) {
        return 0u;
    }

    for (i = 0u; i < len; ++i) {
        crc ^= (uint32_t)p[i];
        for (unsigned bit = 0u; bit < 8u; ++bit) {
            if ((crc & 1u) != 0u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc ^ 0xFFFFFFFFu;
}
