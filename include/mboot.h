/*
 * microboot - Boot and recovery manager for embedded systems.
 *
 * SPDX-License-Identifier: MIT
 * https://github.com/Vanderhell/microboot
 */

#ifndef MICROBOOT_MBOOT_H_INCLUDED
#define MICROBOOT_MBOOT_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <limits.h>

#if UINT8_MAX != 0xFFu
#error "microboot requires 8-bit unsigned char"
#endif

#if UINT32_MAX != 0xFFFFFFFFu
#error "microboot requires 32-bit uint32_t"
#endif

#define MBOOT_WIRE_BYTES 32u
#define MBOOT_WIRE_MAGIC 0x424F4F54u /* "BOOT" */
#define MBOOT_WIRE_VERSION 1u
#define MBOOT_DEFAULT_CRASH_LOOP_THRESHOLD 3u

#define MBOOT_WIRE_FLAG_STARTED            0x01u
#define MBOOT_WIRE_FLAG_CONFIRMED          0x02u
#define MBOOT_WIRE_FLAG_CLEAN_SHUTDOWN     0x04u
#define MBOOT_WIRE_FLAG_LAST_UPTIME_VALID  0x08u
#define MBOOT_WIRE_FLAG_RESERVED_MASK      0xF0u

typedef enum {
    MBOOT_OK = 0,
    MBOOT_ERR_NULL = -1,
    MBOOT_ERR_STATE = -2,
    MBOOT_ERR_IO = -3,
    MBOOT_ERR_INVALID = -4,
    MBOOT_ERR_CORRUPT = -5
} mboot_err_t;

typedef enum {
    MBOOT_STATE_UNINITIALIZED = 0,
    MBOOT_STATE_READY = 1,
    MBOOT_STATE_STARTED_UNCONFIRMED = 2,
    MBOOT_STATE_STARTED_CONFIRMED = 3,
    MBOOT_STATE_SHUTDOWN_RECORDED = 4,
    MBOOT_STATE_ERROR = 5
} mboot_state_t;

typedef enum {
    MBOOT_REASON_COLD = 0,
    MBOOT_REASON_NORMAL = 1,
    MBOOT_REASON_WATCHDOG = 2,
    MBOOT_REASON_CRASH = 3,
    MBOOT_REASON_BROWNOUT = 4,
    MBOOT_REASON_USER = 5
} mboot_reason_t;

typedef enum {
    MBOOT_MODE_NORMAL = 0,
    MBOOT_MODE_RECOVERY = 1,
    MBOOT_MODE_SAFE = 2,
    MBOOT_MODE_FACTORY = 3
} mboot_mode_t;

typedef enum {
    MBOOT_SLOT_IO_OK = 0,
    MBOOT_SLOT_IO_EMPTY = 1,
    MBOOT_SLOT_IO_ERROR = -1
} mboot_slot_io_result_t;

typedef struct {
    uint8_t bytes[MBOOT_WIRE_BYTES];
} mboot_wire_t;

typedef struct {
    uint32_t crash_loop_threshold;
} mboot_config_t;

typedef struct {
    bool has_crash_dump;
    bool clean_shutdown;
    bool confirmed;
    bool started;
    bool last_uptime_valid;
    bool crash_loop;
    uint32_t boot_count;
    uint32_t crash_count;
    uint32_t generation;
    uint32_t last_uptime_ms;
    mboot_reason_t reason;
} mboot_info_t;

typedef mboot_slot_io_result_t (*mboot_read_slot_fn)(uint8_t slot_index,
                                                     mboot_wire_t *out,
                                                     void *ctx);

typedef mboot_slot_io_result_t (*mboot_write_slot_fn)(uint8_t slot_index,
                                                      const mboot_wire_t *record,
                                                      void *ctx);

typedef mboot_err_t (*mboot_has_crash_fn)(bool *has_crash, void *ctx);

typedef mboot_reason_t (*mboot_detect_reason_fn)(void *ctx);

typedef mboot_mode_t (*mboot_decide_fn)(const mboot_info_t *info, void *ctx);

typedef uint32_t (*mboot_clock_fn)(void);

typedef struct {
    mboot_read_slot_fn read_slot;
    mboot_write_slot_fn write_slot;
    mboot_has_crash_fn has_crash;
    mboot_detect_reason_fn detect_reason;
    void *io_ctx;
} mboot_io_t;

typedef struct {
    mboot_config_t config;
    mboot_io_t io;
    mboot_clock_fn clock;
    mboot_decide_fn decide_fn;
    void *decide_ctx;

    mboot_state_t state;
    mboot_mode_t mode;
    mboot_info_t info;

    mboot_wire_t active_wire;
    uint8_t active_slot;
    bool has_active_slot;
    bool busy;
    uint32_t start_tick;
} mboot_t;

const char *mboot_err_str(mboot_err_t err);
const char *mboot_state_str(mboot_state_t state);
const char *mboot_reason_str(mboot_reason_t reason);
const char *mboot_mode_str(mboot_mode_t mode);
const char *mboot_slot_io_result_str(mboot_slot_io_result_t result);

mboot_config_t mboot_default_config(void);

mboot_err_t mboot_init(mboot_t *boot, const mboot_io_t *io,
                       mboot_clock_fn clock, const mboot_config_t *config);

mboot_err_t mboot_set_decide(mboot_t *boot, mboot_decide_fn fn, void *ctx);
mboot_err_t mboot_start(mboot_t *boot);
mboot_err_t mboot_confirm(mboot_t *boot);
mboot_err_t mboot_shutdown(mboot_t *boot);
mboot_err_t mboot_reset_history(mboot_t *boot);

mboot_state_t mboot_state(const mboot_t *boot);
mboot_mode_t mboot_mode(const mboot_t *boot);
mboot_reason_t mboot_reason(const mboot_t *boot);
mboot_err_t mboot_get_info(const mboot_t *boot, mboot_info_t *info);
uint32_t mboot_boot_count(const mboot_t *boot);
uint32_t mboot_crash_count(const mboot_t *boot);
bool mboot_is_crash_loop(const mboot_t *boot);

uint32_t mboot_crc32(const void *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* MICROBOOT_MBOOT_H_INCLUDED */
