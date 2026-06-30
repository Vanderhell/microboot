#include "mboot.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    SLOT_EMPTY = 0,
    SLOT_VALID = 1,
    SLOT_CORRUPT = 2
} slot_state_t;

typedef enum {
    WRITE_OK = 0,
    WRITE_CORRUPT = 1,
    WRITE_PARTIAL = 2
} write_mode_t;

typedef struct {
    mboot_wire_t slots[2];
    slot_state_t state[2];
    unsigned read_calls;
    unsigned write_calls;
    int fail_read_slot;
    int fail_read_count;
    int fail_write_slot;
    int fail_write_count;
    int fail_readback_slot;
    int fail_readback_count;
    int partial_cutoff;
    int partial_slot;
    write_mode_t write_mode;
    uint8_t corrupt_mask;
    bool crash_flag;
    mboot_reason_t hw_reason;
} mock_backend_t;

typedef struct {
    mock_backend_t backend;
    mboot_config_t config;
    mboot_io_t io;
    mboot_t boot;
} fixture_t;

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;
static volatile int current_failed = 0;

static void fail_at(int line, const char *expr, const char *detail)
{
    if (detail != NULL) {
        printf("FAIL\n    line %d: %s (%s)\n", line, expr, detail);
    } else {
        printf("FAIL\n    line %d: %s\n", line, expr);
    }
    current_failed = 1;
}

static void expect_true(int value, const char *expr, int line)
{
    if (!current_failed && !value) {
        fail_at(line, expr, "expected true");
    }
}

static void expect_false(int value, const char *expr, int line)
{
    if (!current_failed && value) {
        fail_at(line, expr, "expected false");
    }
}

static void expect_eq_i32(int expected, int actual, const char *e, const char *a, int line)
{
    if (!current_failed && expected != actual) {
        char detail[96];
        snprintf(detail, sizeof(detail), "%s=%d actual %s=%d", e, expected, a, actual);
        fail_at(line, "EXPECT_EQ", detail);
    }
}

static void expect_eq_u32(uint32_t expected, uint32_t actual, const char *e, const char *a, int line)
{
    if (!current_failed && expected != actual) {
        char detail[96];
        snprintf(detail, sizeof(detail), "%s=%u actual %s=%u", e, expected, a, actual);
        fail_at(line, "EXPECT_EQ", detail);
    }
}

static void expect_eq_mem(const uint8_t *expected, const uint8_t *actual, size_t len, int line)
{
    if (!current_failed && memcmp(expected, actual, len) != 0) {
        fail_at(line, "EXPECT_MEMEQ", "byte mismatch");
    }
}

#define EXPECT_TRUE(expr) expect_true((expr) ? 1 : 0, #expr, __LINE__)
#define EXPECT_FALSE(expr) expect_false((expr) ? 1 : 0, #expr, __LINE__)
#define EXPECT_EQ_I32(expected, actual) expect_eq_i32((expected), (actual), #expected, #actual, __LINE__)
#define EXPECT_EQ_U32(expected, actual) expect_eq_u32((expected), (actual), #expected, #actual, __LINE__)
#define EXPECT_MEMEQ(expected, actual, len) expect_eq_mem((const uint8_t *)(expected), (const uint8_t *)(actual), (len), __LINE__)

static void backend_reset(mock_backend_t *b)
{
    memset(b, 0, sizeof(*b));
    b->state[0] = SLOT_EMPTY;
    b->state[1] = SLOT_EMPTY;
    b->fail_read_slot = -1;
    b->fail_write_slot = -1;
    b->fail_readback_slot = -1;
    b->partial_slot = -1;
    b->write_mode = WRITE_OK;
    b->hw_reason = MBOOT_REASON_COLD;
}

static uint32_t current_clock_value = 0u;

static uint32_t clock_now(void)
{
    return current_clock_value;
}

static bool wire_is_zero(const mboot_wire_t *wire)
{
    size_t i;
    for (i = 0u; i < MBOOT_WIRE_BYTES; ++i) {
        if (wire->bytes[i] != 0u) {
            return false;
        }
    }
    return true;
}

static void wire_encode_expected(mboot_wire_t *wire,
                                 uint32_t generation,
                                 uint32_t boot_count,
                                 uint32_t crash_count,
                                 uint32_t last_uptime_ms,
                                 mboot_reason_t reason,
                                 mboot_mode_t mode,
                                 uint8_t flags)
{
    memset(wire->bytes, 0, sizeof(wire->bytes));
    wire->bytes[0] = (uint8_t)(MBOOT_WIRE_MAGIC & 0xFFu);
    wire->bytes[1] = (uint8_t)((MBOOT_WIRE_MAGIC >> 8) & 0xFFu);
    wire->bytes[2] = (uint8_t)((MBOOT_WIRE_MAGIC >> 16) & 0xFFu);
    wire->bytes[3] = (uint8_t)((MBOOT_WIRE_MAGIC >> 24) & 0xFFu);
    wire->bytes[4] = (uint8_t)MBOOT_WIRE_VERSION;
    wire->bytes[5] = flags;
    wire->bytes[8] = (uint8_t)(generation & 0xFFu);
    wire->bytes[9] = (uint8_t)((generation >> 8) & 0xFFu);
    wire->bytes[10] = (uint8_t)((generation >> 16) & 0xFFu);
    wire->bytes[11] = (uint8_t)((generation >> 24) & 0xFFu);
    wire->bytes[12] = (uint8_t)(boot_count & 0xFFu);
    wire->bytes[13] = (uint8_t)((boot_count >> 8) & 0xFFu);
    wire->bytes[14] = (uint8_t)((boot_count >> 16) & 0xFFu);
    wire->bytes[15] = (uint8_t)((boot_count >> 24) & 0xFFu);
    wire->bytes[16] = (uint8_t)(crash_count & 0xFFu);
    wire->bytes[17] = (uint8_t)((crash_count >> 8) & 0xFFu);
    wire->bytes[18] = (uint8_t)((crash_count >> 16) & 0xFFu);
    wire->bytes[19] = (uint8_t)((crash_count >> 24) & 0xFFu);
    wire->bytes[20] = (uint8_t)(last_uptime_ms & 0xFFu);
    wire->bytes[21] = (uint8_t)((last_uptime_ms >> 8) & 0xFFu);
    wire->bytes[22] = (uint8_t)((last_uptime_ms >> 16) & 0xFFu);
    wire->bytes[23] = (uint8_t)((last_uptime_ms >> 24) & 0xFFu);
    wire->bytes[24] = (uint8_t)reason;
    wire->bytes[25] = (uint8_t)mode;
    wire->bytes[28] = 0u;
    wire->bytes[29] = 0u;
    wire->bytes[30] = 0u;
    wire->bytes[31] = 0u;
    {
        uint32_t crc = mboot_crc32(wire->bytes, 28u);
        wire->bytes[28] = (uint8_t)(crc & 0xFFu);
        wire->bytes[29] = (uint8_t)((crc >> 8) & 0xFFu);
        wire->bytes[30] = (uint8_t)((crc >> 16) & 0xFFu);
        wire->bytes[31] = (uint8_t)((crc >> 24) & 0xFFu);
    }
}

static void backend_set_valid(mock_backend_t *b, int slot,
                              uint32_t generation,
                              uint32_t boot_count,
                              uint32_t crash_count,
                              uint32_t last_uptime_ms,
                              uint8_t flags,
                              mboot_reason_t reason,
                              mboot_mode_t mode)
{
    wire_encode_expected(&b->slots[slot], generation, boot_count, crash_count,
                         last_uptime_ms, reason, mode, flags);
    b->state[slot] = SLOT_VALID;
}

static void backend_set_corrupt(mock_backend_t *b, int slot)
{
    memset(&b->slots[slot], 0xA5, sizeof(b->slots[slot]));
    b->state[slot] = SLOT_CORRUPT;
}

static mboot_slot_io_result_t mock_read_slot(uint8_t slot, mboot_wire_t *out, void *ctx)
{
    mock_backend_t *const b = (mock_backend_t *)ctx;
    b->read_calls++;
    if (b->fail_readback_count > 0 && b->fail_readback_slot == (int)slot) {
        b->fail_readback_count--;
        return MBOOT_SLOT_IO_ERROR;
    }
    if (b->fail_read_count > 0 && b->fail_read_slot == (int)slot) {
        b->fail_read_count--;
        return MBOOT_SLOT_IO_ERROR;
    }
    switch (b->state[slot]) {
    case SLOT_EMPTY:
        return MBOOT_SLOT_IO_EMPTY;
    case SLOT_VALID:
    case SLOT_CORRUPT:
        *out = b->slots[slot];
        return MBOOT_SLOT_IO_OK;
    default:
        return MBOOT_SLOT_IO_ERROR;
    }
}

static mboot_slot_io_result_t mock_write_slot(uint8_t slot, const mboot_wire_t *in, void *ctx)
{
    mock_backend_t *const b = (mock_backend_t *)ctx;
    b->write_calls++;
    if (b->fail_write_count > 0 && b->fail_write_slot == (int)slot) {
        b->fail_write_count--;
        return MBOOT_SLOT_IO_ERROR;
    }
    if (b->write_mode == WRITE_PARTIAL && (b->partial_slot < 0 || b->partial_slot == (int)slot)) {
        int i;
        for (i = 0; i < b->partial_cutoff && i < (int)MBOOT_WIRE_BYTES; ++i) {
            b->slots[slot].bytes[i] = in->bytes[i];
        }
        for (; i < (int)MBOOT_WIRE_BYTES; ++i) {
            b->slots[slot].bytes[i] = 0xCCu;
        }
        b->state[slot] = SLOT_CORRUPT;
        return MBOOT_SLOT_IO_ERROR;
    }
    if (b->write_mode == WRITE_CORRUPT && (b->partial_slot < 0 || b->partial_slot == (int)slot)) {
        b->slots[slot] = *in;
        b->slots[slot].bytes[0] ^= b->corrupt_mask;
        b->state[slot] = SLOT_CORRUPT;
        return MBOOT_SLOT_IO_OK;
    }
    b->slots[slot] = *in;
    b->state[slot] = SLOT_VALID;
    return MBOOT_SLOT_IO_OK;
}

/* cppcheck-suppress constParameterCallback */
static mboot_err_t mock_has_crash(bool *has_crash, void *ctx)
{
    const mock_backend_t *b = (const mock_backend_t *)ctx;
    *has_crash = b->crash_flag;
    return MBOOT_OK;
}

/* cppcheck-suppress constParameterCallback */
static mboot_reason_t mock_detect_reason(void *ctx)
{
    const mock_backend_t *b = (const mock_backend_t *)ctx;
    return b->hw_reason;
}

static mboot_mode_t mock_decide(const mboot_info_t *info, void *ctx)
{
    (void)info;
    return *(mboot_mode_t *)ctx;
}

static void fixture_init(fixture_t *fx)
{
    backend_reset(&fx->backend);
    current_clock_value = 0u;
    fx->config = mboot_default_config();
    fx->io.read_slot = mock_read_slot;
    fx->io.write_slot = mock_write_slot;
    fx->io.has_crash = mock_has_crash;
    fx->io.detect_reason = mock_detect_reason;
    fx->io.io_ctx = &fx->backend;
    EXPECT_EQ_I32(MBOOT_OK, mboot_init(&fx->boot, &fx->io, clock_now, &fx->config));
}

static uint8_t flags_started(bool confirmed, bool clean_shutdown, bool last_uptime_valid)
{
    uint8_t flags = MBOOT_WIRE_FLAG_STARTED;
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

static void backend_set_bytes(mock_backend_t *b, int slot, const uint8_t bytes[MBOOT_WIRE_BYTES])
{
    memcpy(b->slots[slot].bytes, bytes, MBOOT_WIRE_BYTES);
    b->state[slot] = SLOT_VALID;
}

static void wire_make_valid_base(mboot_wire_t *wire,
                                 uint32_t generation,
                                 uint32_t boot_count,
                                 uint32_t crash_count,
                                 uint32_t last_uptime_ms,
                                 mboot_reason_t reason,
                                 mboot_mode_t mode,
                                 uint8_t flags)
{
    wire_encode_expected(wire, generation, boot_count, crash_count, last_uptime_ms,
                         reason, mode, flags);
}

static void wire_put_u32_le(uint8_t *bytes, size_t offset, uint32_t value)
{
    bytes[offset + 0u] = (uint8_t)(value & 0xFFu);
    bytes[offset + 1u] = (uint8_t)((value >> 8) & 0xFFu);
    bytes[offset + 2u] = (uint8_t)((value >> 16) & 0xFFu);
    bytes[offset + 3u] = (uint8_t)((value >> 24) & 0xFFu);
}

enum {
    TEST_WIRE_OFF_MAGIC = 0,
    TEST_WIRE_OFF_VERSION = 4,
    TEST_WIRE_OFF_FLAGS = 5,
    TEST_WIRE_OFF_GENERATION = 8,
    TEST_WIRE_OFF_BOOT_COUNT = 12,
    TEST_WIRE_OFF_CRASH_COUNT = 16,
    TEST_WIRE_OFF_LAST_UPTIME_MS = 20,
    TEST_WIRE_OFF_LAST_REASON = 24,
    TEST_WIRE_OFF_LAST_MODE = 25,
    TEST_WIRE_OFF_CRC32 = 28,
    TEST_WIRE_HEADER_BYTES = 28
};

static void test_init_and_state(void)
{
    fixture_t fx;
    mboot_info_t info;
    fixture_init(&fx);
    EXPECT_EQ_I32(MBOOT_STATE_READY, mboot_state(&fx.boot));
    EXPECT_EQ_I32(MBOOT_ERR_STATE, mboot_get_info(&fx.boot, &info));
}

static void test_init_rejects_zero_threshold(void)
{
    fixture_t fx;
    backend_reset(&fx.backend);
    fx.config = mboot_default_config();
    fx.config.crash_loop_threshold = 0u;
    fx.io.read_slot = mock_read_slot;
    fx.io.write_slot = mock_write_slot;
    fx.io.has_crash = mock_has_crash;
    fx.io.detect_reason = mock_detect_reason;
    fx.io.io_ctx = &fx.backend;
    EXPECT_EQ_I32(MBOOT_ERR_INVALID, mboot_init(&fx.boot, &fx.io, clock_now, &fx.config));
}

static void test_double_start_and_reentrancy_guards(void)
{
    fixture_t fx;
    fixture_init(&fx);
    EXPECT_EQ_I32(MBOOT_OK, mboot_start(&fx.boot));
    EXPECT_EQ_I32(MBOOT_STATE_STARTED_UNCONFIRMED, mboot_state(&fx.boot));
    EXPECT_EQ_U32(3u, fx.backend.read_calls);
    EXPECT_EQ_U32(1u, fx.backend.write_calls);
    EXPECT_EQ_I32(MBOOT_ERR_STATE, mboot_start(&fx.boot));
    EXPECT_EQ_I32(MBOOT_ERR_STATE, mboot_set_decide(&fx.boot, mock_decide, &fx.boot.mode));
    EXPECT_EQ_U32(3u, fx.backend.read_calls);
    EXPECT_EQ_U32(1u, fx.backend.write_calls);
}

static void test_confirm_and_shutdown_before_start(void)
{
    fixture_t fx;
    fixture_init(&fx);
    EXPECT_EQ_I32(MBOOT_ERR_STATE, mboot_confirm(&fx.boot));
    EXPECT_EQ_I32(MBOOT_ERR_STATE, mboot_shutdown(&fx.boot));
    EXPECT_EQ_U32(0u, fx.backend.read_calls);
    EXPECT_EQ_U32(0u, fx.backend.write_calls);
    EXPECT_EQ_I32(MBOOT_STATE_READY, mboot_state(&fx.boot));
}

static void test_bootstrap_both_empty(void)
{
    fixture_t fx;
    mboot_info_t info;
    fixture_init(&fx);
    EXPECT_EQ_I32(MBOOT_OK, mboot_start(&fx.boot));
    EXPECT_EQ_I32(MBOOT_STATE_STARTED_UNCONFIRMED, mboot_state(&fx.boot));
    EXPECT_EQ_I32(MBOOT_MODE_NORMAL, mboot_mode(&fx.boot));
    EXPECT_EQ_I32(MBOOT_REASON_COLD, mboot_reason(&fx.boot));
    EXPECT_EQ_I32(MBOOT_OK, mboot_get_info(&fx.boot, &info));
    EXPECT_TRUE(info.started);
    EXPECT_FALSE(info.confirmed);
    EXPECT_FALSE(info.clean_shutdown);
    EXPECT_FALSE(info.last_uptime_valid);
    EXPECT_EQ_U32(1u, info.boot_count);
    EXPECT_EQ_U32(0u, info.crash_count);
    EXPECT_EQ_I32(SLOT_VALID, fx.backend.state[0]);
    EXPECT_EQ_I32(SLOT_EMPTY, fx.backend.state[1]);
    EXPECT_EQ_U32(1u, fx.backend.write_calls);
}

static void test_one_valid_one_empty(void)
{
    fixture_t fx;
    fixture_init(&fx);
    backend_set_valid(&fx.backend, 1, 7u, 4u, 1u, 120u,
                      flags_started(true, true, true), MBOOT_REASON_NORMAL,
                      MBOOT_MODE_NORMAL);
    EXPECT_EQ_I32(MBOOT_OK, mboot_start(&fx.boot));
    EXPECT_EQ_U32(5u, mboot_boot_count(&fx.boot));
    EXPECT_EQ_U32(0u, mboot_crash_count(&fx.boot));
    EXPECT_EQ_I32(MBOOT_REASON_COLD, mboot_reason(&fx.boot));
    EXPECT_EQ_I32(SLOT_VALID, fx.backend.state[0]);
    EXPECT_EQ_I32(SLOT_VALID, fx.backend.state[1]);
    EXPECT_EQ_U32(3u, fx.backend.read_calls);
    EXPECT_EQ_U32(1u, fx.backend.write_calls);
}

static void test_two_valid_different_generations(void)
{
    fixture_t fx;
    fixture_init(&fx);
    backend_set_valid(&fx.backend, 0, 3u, 2u, 0u, 50u,
                      flags_started(true, true, true), MBOOT_REASON_NORMAL,
                      MBOOT_MODE_NORMAL);
    backend_set_valid(&fx.backend, 1, 9u, 8u, 1u, 60u,
                      flags_started(true, true, true), MBOOT_REASON_COLD,
                      MBOOT_MODE_NORMAL);
    EXPECT_EQ_I32(MBOOT_OK, mboot_start(&fx.boot));
    EXPECT_EQ_U32(9u, mboot_boot_count(&fx.boot));
    EXPECT_EQ_U32(0u, mboot_crash_count(&fx.boot));
    EXPECT_EQ_I32(MBOOT_REASON_COLD, mboot_reason(&fx.boot));
    EXPECT_EQ_I32(SLOT_VALID, fx.backend.state[0]);
    EXPECT_EQ_I32(SLOT_VALID, fx.backend.state[1]);
}

static void test_two_valid_same_generation_different_contents(void)
{
    fixture_t fx;
    fixture_init(&fx);
    backend_set_valid(&fx.backend, 0, 5u, 2u, 0u, 10u,
                      flags_started(true, true, true), MBOOT_REASON_NORMAL,
                      MBOOT_MODE_NORMAL);
    backend_set_valid(&fx.backend, 1, 5u, 3u, 0u, 11u,
                      flags_started(true, true, true), MBOOT_REASON_NORMAL,
                      MBOOT_MODE_NORMAL);
    EXPECT_EQ_I32(MBOOT_ERR_CORRUPT, mboot_start(&fx.boot));
    EXPECT_EQ_I32(MBOOT_STATE_ERROR, mboot_state(&fx.boot));
    EXPECT_EQ_U32(0u, fx.backend.write_calls);
}

static void test_one_valid_one_corrupt(void)
{
    fixture_t fx;
    fixture_init(&fx);
    backend_set_valid(&fx.backend, 0, 2u, 1u, 0u, 11u,
                      flags_started(true, true, true), MBOOT_REASON_NORMAL,
                      MBOOT_MODE_NORMAL);
    backend_set_corrupt(&fx.backend, 1);
    EXPECT_EQ_I32(MBOOT_OK, mboot_start(&fx.boot));
    EXPECT_EQ_U32(2u, mboot_boot_count(&fx.boot));
    EXPECT_EQ_U32(0u, mboot_crash_count(&fx.boot));
    EXPECT_EQ_I32(SLOT_VALID, fx.backend.state[0]);
    EXPECT_EQ_I32(SLOT_VALID, fx.backend.state[1]);
    EXPECT_EQ_U32(3u, fx.backend.read_calls);
    EXPECT_EQ_U32(1u, fx.backend.write_calls);
}

static void test_both_corrupt(void)
{
    fixture_t fx;
    fixture_init(&fx);
    backend_set_corrupt(&fx.backend, 0);
    backend_set_corrupt(&fx.backend, 1);
    EXPECT_EQ_I32(MBOOT_ERR_CORRUPT, mboot_start(&fx.boot));
    EXPECT_EQ_I32(MBOOT_STATE_ERROR, mboot_state(&fx.boot));
    EXPECT_EQ_U32(2u, fx.backend.read_calls);
    EXPECT_EQ_U32(0u, fx.backend.write_calls);
}

static void test_read_failure(void)
{
    fixture_t fx;
    fixture_init(&fx);
    backend_set_valid(&fx.backend, 0, 1u, 1u, 0u, 10u,
                      flags_started(true, true, true), MBOOT_REASON_NORMAL,
                      MBOOT_MODE_NORMAL);
    fx.backend.fail_read_slot = 1;
    fx.backend.fail_read_count = 1;
    EXPECT_EQ_I32(MBOOT_ERR_IO, mboot_start(&fx.boot));
    EXPECT_EQ_I32(MBOOT_STATE_ERROR, mboot_state(&fx.boot));
    EXPECT_EQ_U32(2u, fx.backend.read_calls);
    EXPECT_EQ_U32(0u, fx.backend.write_calls);
    EXPECT_EQ_I32(SLOT_VALID, fx.backend.state[0]);
}

static void test_start_write_failure(void)
{
    fixture_t fx;
    fixture_init(&fx);
    fx.backend.fail_write_slot = 0;
    fx.backend.fail_write_count = 1;
    EXPECT_EQ_I32(MBOOT_ERR_IO, mboot_start(&fx.boot));
    EXPECT_EQ_I32(MBOOT_STATE_ERROR, mboot_state(&fx.boot));
    EXPECT_EQ_U32(2u, fx.backend.read_calls);
    EXPECT_EQ_U32(1u, fx.backend.write_calls);
    EXPECT_EQ_U32(0u, mboot_boot_count(&fx.boot));
}

static void test_confirm_write_failure(void)
{
    fixture_t fx;
    fixture_init(&fx);
    EXPECT_EQ_I32(MBOOT_OK, mboot_start(&fx.boot));
    fx.backend.fail_write_slot = 1;
    fx.backend.fail_write_count = 1;
    EXPECT_EQ_I32(MBOOT_ERR_IO, mboot_confirm(&fx.boot));
    EXPECT_EQ_I32(MBOOT_STATE_STARTED_UNCONFIRMED, mboot_state(&fx.boot));
    EXPECT_EQ_U32(3u, fx.backend.read_calls);
    EXPECT_EQ_U32(2u, fx.backend.write_calls);
    EXPECT_EQ_U32(0u, mboot_crash_count(&fx.boot));
}

static void test_shutdown_write_failure(void)
{
    fixture_t fx;
    fixture_init(&fx);
    EXPECT_EQ_I32(MBOOT_OK, mboot_start(&fx.boot));
    current_clock_value = 100u;
    fx.backend.fail_write_slot = 1;
    fx.backend.fail_write_count = 1;
    EXPECT_EQ_I32(MBOOT_ERR_IO, mboot_shutdown(&fx.boot));
    EXPECT_EQ_I32(MBOOT_STATE_STARTED_UNCONFIRMED, mboot_state(&fx.boot));
    EXPECT_EQ_U32(3u, fx.backend.read_calls);
    EXPECT_EQ_U32(2u, fx.backend.write_calls);
}

static void test_readback_failure(void)
{
    fixture_t fx;
    fixture_init(&fx);
    EXPECT_EQ_I32(MBOOT_OK, mboot_start(&fx.boot));
    fx.backend.fail_readback_slot = 1;
    fx.backend.fail_readback_count = 1;
    EXPECT_EQ_I32(MBOOT_ERR_IO, mboot_confirm(&fx.boot));
    EXPECT_EQ_I32(MBOOT_STATE_STARTED_UNCONFIRMED, mboot_state(&fx.boot));
    EXPECT_EQ_I32(SLOT_VALID, fx.backend.state[0]);
    EXPECT_EQ_I32(SLOT_VALID, fx.backend.state[1]);
}

static void test_write_corrupt_return(void)
{
    fixture_t fx;
    fixture_init(&fx);
    EXPECT_EQ_I32(MBOOT_OK, mboot_start(&fx.boot));
    fx.backend.write_mode = WRITE_CORRUPT;
    fx.backend.partial_slot = 1;
    fx.backend.corrupt_mask = 0x01u;
    EXPECT_EQ_I32(MBOOT_ERR_IO, mboot_confirm(&fx.boot));
    EXPECT_EQ_I32(MBOOT_STATE_STARTED_UNCONFIRMED, mboot_state(&fx.boot));
    EXPECT_EQ_I32(SLOT_CORRUPT, fx.backend.state[1]);
    EXPECT_EQ_U32(2u, fx.backend.write_calls);
}

static void test_malformed_wire_formats(void)
{
    fixture_t fx;
    mboot_wire_t wire;

    fixture_init(&fx);
    wire_make_valid_base(&wire, 1u, 1u, 0u, 0u, MBOOT_REASON_COLD,
                         MBOOT_MODE_NORMAL, flags_started(false, false, false));

    wire.bytes[0] ^= 0x01u;
    backend_set_bytes(&fx.backend, 0, wire.bytes);
    EXPECT_EQ_I32(MBOOT_ERR_CORRUPT, mboot_start(&fx.boot));
    EXPECT_EQ_U32(0u, fx.backend.write_calls);

    fixture_init(&fx);
      wire_make_valid_base(&wire, 1u, 1u, 0u, 0u, MBOOT_REASON_COLD,
                           MBOOT_MODE_NORMAL, flags_started(false, false, false));
      wire.bytes[TEST_WIRE_OFF_VERSION] = 2u;
      backend_set_bytes(&fx.backend, 0, wire.bytes);
      EXPECT_EQ_I32(MBOOT_ERR_CORRUPT, mboot_start(&fx.boot));
      EXPECT_EQ_U32(0u, fx.backend.write_calls);

      fixture_init(&fx);
      wire_make_valid_base(&wire, 1u, 1u, 0u, 0u, MBOOT_REASON_COLD,
                           MBOOT_MODE_NORMAL, flags_started(false, false, false));
      wire.bytes[TEST_WIRE_OFF_FLAGS] = 0x10u;
      backend_set_bytes(&fx.backend, 0, wire.bytes);
      EXPECT_EQ_I32(MBOOT_ERR_CORRUPT, mboot_start(&fx.boot));
      EXPECT_EQ_U32(0u, fx.backend.write_calls);

      fixture_init(&fx);
      wire_make_valid_base(&wire, 1u, 1u, 0u, 0u, MBOOT_REASON_COLD,
                           MBOOT_MODE_NORMAL, flags_started(false, false, false));
      wire.bytes[24] = 99u;
      backend_set_bytes(&fx.backend, 0, wire.bytes);
      EXPECT_EQ_I32(MBOOT_ERR_CORRUPT, mboot_start(&fx.boot));
      EXPECT_EQ_U32(0u, fx.backend.write_calls);

      fixture_init(&fx);
      wire_make_valid_base(&wire, 1u, 1u, 0u, 0u, MBOOT_REASON_COLD,
                           MBOOT_MODE_NORMAL, flags_started(false, false, false));
      wire.bytes[25] = 99u;
      backend_set_bytes(&fx.backend, 0, wire.bytes);
      EXPECT_EQ_I32(MBOOT_ERR_CORRUPT, mboot_start(&fx.boot));
      EXPECT_EQ_U32(0u, fx.backend.write_calls);

      fixture_init(&fx);
      wire_make_valid_base(&wire, 1u, 1u, 0u, 0u, MBOOT_REASON_COLD,
                           MBOOT_MODE_NORMAL, flags_started(false, false, false));
      wire.bytes[6] = 1u;
      backend_set_bytes(&fx.backend, 0, wire.bytes);
      EXPECT_EQ_I32(MBOOT_ERR_CORRUPT, mboot_start(&fx.boot));
      EXPECT_EQ_U32(0u, fx.backend.write_calls);

      fixture_init(&fx);
      wire_make_valid_base(&wire, 1u, 1u, 0u, 0u, MBOOT_REASON_COLD,
                           MBOOT_MODE_NORMAL, flags_started(false, false, false));
      wire.bytes[28] ^= 0xFFu;
      backend_set_bytes(&fx.backend, 0, wire.bytes);
      EXPECT_EQ_I32(MBOOT_ERR_CORRUPT, mboot_start(&fx.boot));
      EXPECT_EQ_U32(0u, fx.backend.write_calls);
  }

static void test_endian_independent_decode(void)
{
    fixture_t fx;
    uint8_t bytes[MBOOT_WIRE_BYTES];
    uint32_t crc;
    mboot_info_t info;
    mboot_mode_t forced_mode = MBOOT_MODE_SAFE;

    fixture_init(&fx);
    memset(bytes, 0, sizeof(bytes));
    wire_put_u32_le(bytes, TEST_WIRE_OFF_MAGIC, MBOOT_WIRE_MAGIC);
    bytes[TEST_WIRE_OFF_VERSION] = (uint8_t)MBOOT_WIRE_VERSION;
    bytes[TEST_WIRE_OFF_FLAGS] = flags_started(true, true, true);
    wire_put_u32_le(bytes, TEST_WIRE_OFF_GENERATION, 0x11223344u);
    wire_put_u32_le(bytes, TEST_WIRE_OFF_BOOT_COUNT, 0x55667788u);
    wire_put_u32_le(bytes, TEST_WIRE_OFF_CRASH_COUNT, 0x99AABBCCu);
    wire_put_u32_le(bytes, TEST_WIRE_OFF_LAST_UPTIME_MS, 0x01020304u);
    bytes[TEST_WIRE_OFF_LAST_REASON] = (uint8_t)MBOOT_REASON_BROWNOUT;
    bytes[TEST_WIRE_OFF_LAST_MODE] = (uint8_t)MBOOT_MODE_SAFE;
    crc = mboot_crc32(bytes, TEST_WIRE_HEADER_BYTES);
    wire_put_u32_le(bytes, TEST_WIRE_OFF_CRC32, crc);
    backend_set_bytes(&fx.backend, 0, bytes);
    fx.backend.hw_reason = MBOOT_REASON_BROWNOUT;
    EXPECT_EQ_I32(MBOOT_OK, mboot_set_decide(&fx.boot, mock_decide, &forced_mode));

    EXPECT_EQ_I32(MBOOT_OK, mboot_start(&fx.boot));
    EXPECT_EQ_I32(MBOOT_OK, mboot_get_info(&fx.boot, &info));
    EXPECT_EQ_U32(0x11223345u, info.generation);
    EXPECT_EQ_U32(0x55667789u, info.boot_count);
    EXPECT_EQ_U32(0u, info.crash_count);
    EXPECT_EQ_U32(0x01020304u, info.last_uptime_ms);
    EXPECT_EQ_I32(MBOOT_REASON_BROWNOUT, info.reason);
    EXPECT_EQ_I32(MBOOT_MODE_SAFE, mboot_mode(&fx.boot));
}

static void test_partial_write_boundaries(void)
{
    int cutoff;
    for (cutoff = 0; cutoff < (int)MBOOT_WIRE_BYTES; ++cutoff) {
        fixture_t fx;
        mboot_t next;
        fixture_init(&fx);
        EXPECT_EQ_I32(MBOOT_OK, mboot_start(&fx.boot));
        fx.backend.write_mode = WRITE_PARTIAL;
        fx.backend.partial_slot = 1;
        fx.backend.partial_cutoff = cutoff;
        EXPECT_EQ_I32(MBOOT_ERR_IO, mboot_confirm(&fx.boot));
        EXPECT_EQ_I32(MBOOT_STATE_STARTED_UNCONFIRMED, mboot_state(&fx.boot));
        fx.backend.write_mode = WRITE_OK;
        fx.backend.partial_slot = -1;
        EXPECT_EQ_I32(MBOOT_OK, mboot_init(&next, &fx.io, clock_now, &fx.config));
        EXPECT_EQ_I32(MBOOT_OK, mboot_start(&next));
        EXPECT_EQ_U32(2u, mboot_boot_count(&next));
        EXPECT_EQ_U32(1u, mboot_crash_count(&next));
    }
}

static void test_repeated_pre_confirm_crash_loop(void)
{
    fixture_t fx;
    int i;

    fixture_init(&fx);
    for (i = 0; i < (int)MBOOT_DEFAULT_CRASH_LOOP_THRESHOLD; ++i) {
        mboot_t boot;
        EXPECT_EQ_I32(MBOOT_OK, mboot_init(&boot, &fx.io, clock_now, &fx.config));
        EXPECT_EQ_I32(MBOOT_OK, mboot_start(&boot));
        EXPECT_EQ_I32(MBOOT_STATE_STARTED_UNCONFIRMED, mboot_state(&boot));
        current_clock_value += 5u;
    }
    {
        mboot_t final;
        EXPECT_EQ_I32(MBOOT_OK, mboot_init(&final, &fx.io, clock_now, &fx.config));
        EXPECT_EQ_I32(MBOOT_OK, mboot_start(&final));
        EXPECT_TRUE(mboot_is_crash_loop(&final));
        EXPECT_EQ_I32(MBOOT_MODE_RECOVERY, mboot_mode(&final));
    }
}

static void test_confirmed_boot_followed_by_later_crash(void)
{
    fixture_t fx;
    mboot_t next;
    fixture_init(&fx);
    EXPECT_EQ_I32(MBOOT_OK, mboot_start(&fx.boot));
    EXPECT_EQ_I32(MBOOT_OK, mboot_confirm(&fx.boot));
    EXPECT_EQ_I32(MBOOT_STATE_STARTED_CONFIRMED, mboot_state(&fx.boot));
    fx.backend.crash_flag = true;
    fx.backend.hw_reason = MBOOT_REASON_WATCHDOG;
    EXPECT_EQ_I32(MBOOT_OK, mboot_init(&next, &fx.io, clock_now, &fx.config));
    EXPECT_EQ_I32(MBOOT_OK, mboot_start(&next));
    EXPECT_EQ_U32(2u, mboot_boot_count(&next));
    EXPECT_EQ_U32(0u, mboot_crash_count(&next));
    EXPECT_EQ_I32(MBOOT_REASON_WATCHDOG, mboot_reason(&next));
}

static void test_stale_crash_dump_after_clean_shutdown(void)
{
    fixture_t fx;
    mboot_t next;
    fixture_init(&fx);
    EXPECT_EQ_I32(MBOOT_OK, mboot_start(&fx.boot));
    current_clock_value = 200u;
    EXPECT_EQ_I32(MBOOT_OK, mboot_shutdown(&fx.boot));
    fx.backend.crash_flag = true;
    fx.backend.hw_reason = MBOOT_REASON_COLD;
    EXPECT_EQ_I32(MBOOT_OK, mboot_init(&next, &fx.io, clock_now, &fx.config));
    EXPECT_EQ_I32(MBOOT_OK, mboot_start(&next));
    EXPECT_EQ_U32(2u, mboot_boot_count(&next));
    EXPECT_EQ_U32(0u, mboot_crash_count(&next));
    EXPECT_EQ_I32(MBOOT_REASON_COLD, mboot_reason(&next));
}

static void test_clean_watchdog_reboot(void)
{
    fixture_t fx;
    mboot_t next;

    fixture_init(&fx);
    EXPECT_EQ_I32(MBOOT_OK, mboot_start(&fx.boot));
    current_clock_value = 200u;
    EXPECT_EQ_I32(MBOOT_OK, mboot_shutdown(&fx.boot));
    fx.backend.crash_flag = true;
    fx.backend.hw_reason = MBOOT_REASON_WATCHDOG;
    EXPECT_EQ_I32(MBOOT_OK, mboot_init(&next, &fx.io, clock_now, &fx.config));
    EXPECT_EQ_I32(MBOOT_OK, mboot_start(&next));
    EXPECT_EQ_U32(0u, mboot_crash_count(&next));
    EXPECT_EQ_I32(MBOOT_REASON_WATCHDOG, mboot_reason(&next));
}

static void test_stale_crash_dump_after_confirmed_boot(void)
{
    fixture_t fx;
    mboot_t next;

    fixture_init(&fx);
    EXPECT_EQ_I32(MBOOT_OK, mboot_start(&fx.boot));
    EXPECT_EQ_I32(MBOOT_OK, mboot_confirm(&fx.boot));
    fx.backend.crash_flag = true;
    fx.backend.hw_reason = MBOOT_REASON_COLD;
    EXPECT_EQ_I32(MBOOT_OK, mboot_init(&next, &fx.io, clock_now, &fx.config));
    EXPECT_EQ_I32(MBOOT_OK, mboot_start(&next));
    EXPECT_EQ_U32(0u, mboot_crash_count(&next));
    EXPECT_EQ_I32(MBOOT_REASON_CRASH, mboot_reason(&next));
}

static void test_reason_detection_variants(void)
{
    fixture_t fx;

    fixture_init(&fx);
    fx.backend.hw_reason = MBOOT_REASON_WATCHDOG;
    EXPECT_EQ_I32(MBOOT_OK, mboot_start(&fx.boot));
    EXPECT_EQ_I32(MBOOT_REASON_WATCHDOG, mboot_reason(&fx.boot));

    fixture_init(&fx);
    fx.backend.hw_reason = MBOOT_REASON_BROWNOUT;
    EXPECT_EQ_I32(MBOOT_OK, mboot_start(&fx.boot));
    EXPECT_EQ_I32(MBOOT_REASON_BROWNOUT, mboot_reason(&fx.boot));

    fixture_init(&fx);
    fx.backend.hw_reason = MBOOT_REASON_USER;
    EXPECT_EQ_I32(MBOOT_OK, mboot_start(&fx.boot));
    EXPECT_EQ_I32(MBOOT_REASON_USER, mboot_reason(&fx.boot));
}

static void test_missing_detect_reason_callback(void)
{
    fixture_t fx;
    mboot_io_t io;
    mboot_t boot;

    backend_reset(&fx.backend);
    fx.config = mboot_default_config();
    io.read_slot = mock_read_slot;
    io.write_slot = mock_write_slot;
    io.has_crash = mock_has_crash;
    io.detect_reason = NULL;
    io.io_ctx = &fx.backend;
    EXPECT_EQ_I32(MBOOT_OK, mboot_init(&boot, &io, clock_now, &fx.config));
    EXPECT_EQ_I32(MBOOT_OK, mboot_start(&boot));
    EXPECT_EQ_I32(MBOOT_REASON_COLD, mboot_reason(&boot));
}

static void test_confirm_after_shutdown_rejected(void)
{
    fixture_t fx;
    uint32_t writes_after_shutdown;

    fixture_init(&fx);
    EXPECT_EQ_I32(MBOOT_OK, mboot_start(&fx.boot));
    current_clock_value = 123u;
    EXPECT_EQ_I32(MBOOT_OK, mboot_shutdown(&fx.boot));
    writes_after_shutdown = fx.backend.write_calls;
    EXPECT_EQ_I32(MBOOT_ERR_STATE, mboot_confirm(&fx.boot));
    EXPECT_EQ_U32(writes_after_shutdown, fx.backend.write_calls);
    EXPECT_EQ_I32(MBOOT_STATE_SHUTDOWN_RECORDED, mboot_state(&fx.boot));
}

static void test_threshold_one_behavior(void)
{
    fixture_t fx;
    mboot_config_t config;
    mboot_t next;

    backend_reset(&fx.backend);
    config = mboot_default_config();
    config.crash_loop_threshold = 1u;
    fx.io.read_slot = mock_read_slot;
    fx.io.write_slot = mock_write_slot;
    fx.io.has_crash = mock_has_crash;
    fx.io.detect_reason = mock_detect_reason;
    fx.io.io_ctx = &fx.backend;
    EXPECT_EQ_I32(MBOOT_OK, mboot_init(&fx.boot, &fx.io, clock_now, &config));
    EXPECT_EQ_I32(MBOOT_OK, mboot_start(&fx.boot));
    EXPECT_EQ_I32(MBOOT_OK, mboot_init(&next, &fx.io, clock_now, &config));
    EXPECT_EQ_I32(MBOOT_OK, mboot_start(&next));
    EXPECT_TRUE(mboot_is_crash_loop(&next));
    EXPECT_EQ_I32(MBOOT_MODE_RECOVERY, mboot_mode(&next));
}

static void test_generation_wrap(void)
{
    fixture_t fx;
    fixture_init(&fx);
    backend_set_valid(&fx.backend, 0, UINT32_MAX, 2u, 0u, 1u,
                      flags_started(true, true, true), MBOOT_REASON_NORMAL,
                      MBOOT_MODE_NORMAL);
    backend_set_valid(&fx.backend, 1, 0u, 1u, 0u, 1u,
                      flags_started(true, true, true), MBOOT_REASON_NORMAL,
                      MBOOT_MODE_NORMAL);
    EXPECT_EQ_I32(MBOOT_OK, mboot_start(&fx.boot));
    EXPECT_EQ_U32(2u, mboot_boot_count(&fx.boot));
    EXPECT_EQ_U32(0u, mboot_crash_count(&fx.boot));
    EXPECT_EQ_I32(MBOOT_REASON_COLD, mboot_reason(&fx.boot));
}

static void test_invalid_reason_and_mode(void)
{
    fixture_t fx;
    mboot_mode_t invalid_mode = (mboot_mode_t)99; /* NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange) */
    mboot_decide_fn decide = mock_decide;

    fixture_init(&fx);
    fx.backend.hw_reason = (mboot_reason_t)99; /* NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange) */
    EXPECT_EQ_I32(MBOOT_ERR_INVALID, mboot_start(&fx.boot));

    fixture_init(&fx);
    EXPECT_EQ_I32(MBOOT_OK, mboot_set_decide(&fx.boot, decide, &invalid_mode));
    EXPECT_EQ_I32(MBOOT_ERR_INVALID, mboot_start(&fx.boot));
}

static void test_counter_saturation(void)
{
    fixture_t fx;
    fixture_init(&fx);
    backend_set_valid(&fx.backend, 0, UINT32_MAX - 1u, UINT32_MAX - 1u,
                      UINT32_MAX - 1u, 10u,
                      flags_started(false, false, false), MBOOT_REASON_COLD,
                      MBOOT_MODE_NORMAL);
    EXPECT_EQ_I32(MBOOT_OK, mboot_start(&fx.boot));
    EXPECT_EQ_U32(UINT32_MAX, mboot_boot_count(&fx.boot));
    EXPECT_EQ_U32(UINT32_MAX, mboot_crash_count(&fx.boot));
}

static void test_uptime_wrap(void)
{
    fixture_t fx;
    mboot_info_t info;
    fixture_init(&fx);
    current_clock_value = UINT32_MAX - 10u;
    EXPECT_EQ_I32(MBOOT_OK, mboot_start(&fx.boot));
    current_clock_value = 15u;
    EXPECT_EQ_I32(MBOOT_OK, mboot_shutdown(&fx.boot));
    EXPECT_EQ_I32(MBOOT_OK, mboot_get_info(&fx.boot, &info));
    EXPECT_TRUE(info.last_uptime_valid);
    EXPECT_EQ_U32(26u, info.last_uptime_ms);
    EXPECT_EQ_U32(26u, fx.backend.slots[1].bytes[20] |
                       ((uint32_t)fx.backend.slots[1].bytes[21] << 8) |
                       ((uint32_t)fx.backend.slots[1].bytes[22] << 16) |
                       ((uint32_t)fx.backend.slots[1].bytes[23] << 24));
}

static void test_crc32_contract(void)
{
    const uint8_t data[] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    EXPECT_EQ_U32(0u, mboot_crc32(NULL, 0u));
    EXPECT_EQ_U32(0u, mboot_crc32(data, 0u));
    EXPECT_EQ_U32(0xCBF43926u, mboot_crc32(data, 9u));
}

static void test_golden_wire_vector(void)
{
    fixture_t fx;
    mboot_wire_t expected;

    fixture_init(&fx);
    EXPECT_EQ_I32(MBOOT_OK, mboot_start(&fx.boot));
    wire_encode_expected(&expected, 1u, 1u, 0u, 0u, MBOOT_REASON_COLD,
                         MBOOT_MODE_NORMAL, flags_started(false, false, false));
    EXPECT_MEMEQ(expected.bytes, fx.backend.slots[0].bytes, MBOOT_WIRE_BYTES);
}

static void test_callback_bundle_lifetime(void)
{
    fixture_t fx;
    mboot_io_t io;
    mboot_config_t config;
    mboot_t boot;

    backend_reset(&fx.backend);
    config = mboot_default_config();
    io.read_slot = mock_read_slot;
    io.write_slot = mock_write_slot;
    io.has_crash = mock_has_crash;
    io.detect_reason = mock_detect_reason;
    io.io_ctx = &fx.backend;
    EXPECT_EQ_I32(MBOOT_OK, mboot_init(&boot, &io, clock_now, &config));
    io.read_slot = NULL;
    config.crash_loop_threshold = 99u;
    EXPECT_EQ_I32(MBOOT_OK, mboot_start(&boot));
    EXPECT_EQ_U32(1u, mboot_boot_count(&boot));
}

static void test_two_independent_instances(void)
{
    fixture_t a;
    fixture_t b;

    fixture_init(&a);
    fixture_init(&b);
    EXPECT_EQ_I32(MBOOT_OK, mboot_start(&a.boot));
    EXPECT_EQ_I32(MBOOT_OK, mboot_start(&b.boot));
    current_clock_value = 50u;
    EXPECT_EQ_I32(MBOOT_OK, mboot_confirm(&a.boot));
    EXPECT_EQ_I32(MBOOT_STATE_STARTED_UNCONFIRMED, mboot_state(&b.boot));
    EXPECT_EQ_U32(1u, mboot_boot_count(&b.boot));
}

static void test_reset_history(void)
{
    fixture_t fx;
    fixture_init(&fx);
    EXPECT_EQ_I32(MBOOT_OK, mboot_start(&fx.boot));
    EXPECT_EQ_I32(MBOOT_OK, mboot_reset_history(&fx.boot));
    EXPECT_EQ_I32(MBOOT_STATE_READY, mboot_state(&fx.boot));
    EXPECT_TRUE(wire_is_zero(&fx.backend.slots[0]));
    EXPECT_TRUE(wire_is_zero(&fx.backend.slots[1]));
}

static void test_get_info_requires_successful_start(void)
{
    fixture_t fx;
    mboot_info_t info;

    fixture_init(&fx);
    EXPECT_EQ_I32(MBOOT_ERR_STATE, mboot_get_info(&fx.boot, &info));
    EXPECT_EQ_I32(MBOOT_OK, mboot_start(&fx.boot));
    EXPECT_EQ_I32(MBOOT_OK, mboot_get_info(&fx.boot, &info));
    EXPECT_TRUE(info.started);
}

static void run_test(const char *name, void (*fn)(void))
{
    tests_run++;
    current_failed = 0;
    printf("  %-48s ", name);
    fn();
    if (current_failed) {
        printf("FAIL\n");
        tests_failed++;
    } else {
        printf("PASS\n");
        tests_passed++;
    }
}

int main(void)
{
    printf("\n=== microboot test suite ===\n\n");
    run_test("test_init_and_state", test_init_and_state);
    run_test("test_init_rejects_zero_threshold", test_init_rejects_zero_threshold);
    run_test("test_double_start_and_reentrancy_guards", test_double_start_and_reentrancy_guards);
    run_test("test_confirm_and_shutdown_before_start", test_confirm_and_shutdown_before_start);
    run_test("test_bootstrap_both_empty", test_bootstrap_both_empty);
    run_test("test_one_valid_one_empty", test_one_valid_one_empty);
    run_test("test_two_valid_different_generations", test_two_valid_different_generations);
    run_test("test_two_valid_same_generation_different_contents", test_two_valid_same_generation_different_contents);
    run_test("test_one_valid_one_corrupt", test_one_valid_one_corrupt);
    run_test("test_both_corrupt", test_both_corrupt);
    run_test("test_read_failure", test_read_failure);
    run_test("test_start_write_failure", test_start_write_failure);
    run_test("test_confirm_write_failure", test_confirm_write_failure);
    run_test("test_shutdown_write_failure", test_shutdown_write_failure);
    run_test("test_readback_failure", test_readback_failure);
    run_test("test_write_corrupt_return", test_write_corrupt_return);
    run_test("test_malformed_wire_formats", test_malformed_wire_formats);
    run_test("test_endian_independent_decode", test_endian_independent_decode);
    run_test("test_partial_write_boundaries", test_partial_write_boundaries);
    run_test("test_repeated_pre_confirm_crash_loop", test_repeated_pre_confirm_crash_loop);
    run_test("test_confirmed_boot_followed_by_later_crash", test_confirmed_boot_followed_by_later_crash);
    run_test("test_stale_crash_dump_after_clean_shutdown", test_stale_crash_dump_after_clean_shutdown);
    run_test("test_clean_watchdog_reboot", test_clean_watchdog_reboot);
    run_test("test_stale_crash_dump_after_confirmed_boot", test_stale_crash_dump_after_confirmed_boot);
    run_test("test_reason_detection_variants", test_reason_detection_variants);
    run_test("test_missing_detect_reason_callback", test_missing_detect_reason_callback);
    run_test("test_confirm_after_shutdown_rejected", test_confirm_after_shutdown_rejected);
    run_test("test_threshold_one_behavior", test_threshold_one_behavior);
    run_test("test_invalid_reason_and_mode", test_invalid_reason_and_mode);
    run_test("test_counter_saturation", test_counter_saturation);
    run_test("test_generation_wrap", test_generation_wrap);
    run_test("test_uptime_wrap", test_uptime_wrap);
    run_test("test_crc32_contract", test_crc32_contract);
    run_test("test_golden_wire_vector", test_golden_wire_vector);
    run_test("test_callback_bundle_lifetime", test_callback_bundle_lifetime);
    run_test("test_two_independent_instances", test_two_independent_instances);
    run_test("test_reset_history", test_reset_history);
    run_test("test_get_info_requires_successful_start", test_get_info_requires_successful_start);

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed != 0) {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ===\n\n");
    return tests_failed == 0 ? 0 : 1;
}
