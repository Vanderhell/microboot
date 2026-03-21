/*
 * microboot test suite.
 *
 * Build: gcc -std=c99 -Wall -Wextra -I../include ../src/mboot.c test_all.c -o test_all
 */

#include "mboot.h"
#include <stdio.h>
#include <string.h>

/* ── Test framework ────────────────────────────────────────────────────── */

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN_TEST(name) do {                                     \
    tests_run++;                                                \
    printf("  %-55s ", #name);                                  \
    name();                                                     \
    printf("PASS\n");                                           \
    tests_passed++;                                             \
} while (0)

#define ASSERT_EQ(expected, actual) do {                        \
    if ((expected) != (actual)) {                               \
        printf("FAIL\n    %s:%d: expected %d, got %d\n",       \
               __FILE__, __LINE__, (int)(expected), (int)(actual)); \
        tests_failed++; return;                                 \
    }                                                           \
} while (0)

#define ASSERT_TRUE(expr) do {                                  \
    if (!(expr)) {                                              \
        printf("FAIL\n    %s:%d: expected true\n",              \
               __FILE__, __LINE__);                             \
        tests_failed++; return;                                 \
    }                                                           \
} while (0)

#define ASSERT_FALSE(expr) do {                                 \
    if ((expr)) {                                               \
        printf("FAIL\n    %s:%d: expected false\n",             \
               __FILE__, __LINE__);                             \
        tests_failed++; return;                                 \
    }                                                           \
} while (0)

#define ASSERT_STR_EQ(expected, actual) do {                    \
    if (strcmp((expected), (actual)) != 0) {                     \
        printf("FAIL\n    %s:%d: expected \"%s\", got \"%s\"\n",\
               __FILE__, __LINE__, (expected), (actual));       \
        tests_failed++; return;                                 \
    }                                                           \
} while (0)

/* ── Mock platform ─────────────────────────────────────────────────────── */

static uint32_t mock_time = 1000;
static uint32_t mock_clock(void) { return mock_time; }

/* Persistent storage simulation */
static mboot_record_t mock_storage;
static bool storage_valid = false;
static bool io_fail_read = false;
static bool io_fail_write = false;

static int mock_read(mboot_record_t *rec, void *ctx)
{
    (void)ctx;
    if (io_fail_read) return -1;
    if (!storage_valid) return -1;
    *rec = mock_storage;
    return 0;
}

static int mock_write(const mboot_record_t *rec, void *ctx)
{
    (void)ctx;
    if (io_fail_write) return -1;
    mock_storage = *rec;
    storage_valid = true;
    return 0;
}

/* Crash dump simulation */
static bool mock_has_crash_flag = false;
static bool mock_has_crash(void *ctx) { (void)ctx; return mock_has_crash_flag; }

/* Boot reason simulation */
static mboot_reason_t mock_hw_reason = MBOOT_REASON_COLD;
static mboot_reason_t mock_detect_reason(void *ctx) { (void)ctx; return mock_hw_reason; }

/* Custom decision tracking */
static int decide_call_count = 0;
static mboot_mode_t custom_decide_result = MBOOT_MODE_NORMAL;
static mboot_mode_t custom_decide(const mboot_info_t *info, void *ctx)
{
    (void)info; (void)ctx;
    decide_call_count++;
    return custom_decide_result;
}

/* ── Setup ─────────────────────────────────────────────────────────────── */

static mboot_t boot;
static const mboot_io_t test_io = {
    .read_record   = mock_read,
    .write_record  = mock_write,
    .has_crash     = mock_has_crash,
    .detect_reason = mock_detect_reason,
    .io_ctx        = NULL,
};

static void reset_all(void)
{
    mock_time = 1000;
    storage_valid = false;
    io_fail_read = false;
    io_fail_write = false;
    mock_has_crash_flag = false;
    mock_hw_reason = MBOOT_REASON_COLD;
    decide_call_count = 0;
    custom_decide_result = MBOOT_MODE_NORMAL;
    memset(&mock_storage, 0, sizeof(mock_storage));
    memset(&boot, 0, sizeof(boot));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Tests: Init
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(test_init) {
    reset_all();
    ASSERT_EQ(MBOOT_OK, mboot_init(&boot, &test_io, mock_clock));
    ASSERT_EQ(MBOOT_MODE_NORMAL, mboot_mode(&boot));
    ASSERT_FALSE(boot.started);
}

TEST(test_init_null) {
    reset_all();
    ASSERT_EQ(MBOOT_ERR_NULL, mboot_init(NULL, &test_io, mock_clock));
    ASSERT_EQ(MBOOT_ERR_NULL, mboot_init(&boot, NULL, mock_clock));
    ASSERT_EQ(MBOOT_ERR_NULL, mboot_init(&boot, &test_io, NULL));
}

TEST(test_init_null_io_callbacks) {
    reset_all();
    mboot_io_t bad_io = test_io;
    bad_io.read_record = NULL;
    ASSERT_EQ(MBOOT_ERR_NULL, mboot_init(&boot, &bad_io, mock_clock));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Tests: First boot (no stored record)
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(test_first_boot) {
    reset_all();
    mboot_init(&boot, &test_io, mock_clock);
    ASSERT_EQ(MBOOT_OK, mboot_start(&boot));

    ASSERT_EQ(MBOOT_MODE_NORMAL, mboot_mode(&boot));
    ASSERT_EQ(MBOOT_REASON_COLD, mboot_reason(&boot));
    ASSERT_EQ(1, (int)mboot_boot_count(&boot));
    ASSERT_EQ(0, (int)mboot_crash_count(&boot));
    ASSERT_FALSE(mboot_is_crash_loop(&boot));
    ASSERT_TRUE(boot.started);
}

TEST(test_first_boot_writes_record) {
    reset_all();
    mboot_init(&boot, &test_io, mock_clock);
    mboot_start(&boot);

    ASSERT_TRUE(storage_valid);
    ASSERT_EQ((int)MBOOT_MAGIC, (int)mock_storage.magic);
    ASSERT_EQ(1, (int)mock_storage.boot_count);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Tests: Normal reboot (clean shutdown → cold boot)
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(test_clean_reboot) {
    reset_all();
    mboot_init(&boot, &test_io, mock_clock);
    mboot_start(&boot);

    /* Simulate uptime + clean shutdown */
    mock_time += 60000;
    mboot_shutdown(&boot);
    ASSERT_EQ(1, mock_storage.clean_shutdown);

    /* Second boot */
    mboot_t boot2;
    mboot_init(&boot2, &test_io, mock_clock);
    mboot_start(&boot2);

    ASSERT_EQ(MBOOT_MODE_NORMAL, mboot_mode(&boot2));
    ASSERT_EQ(2, (int)mboot_boot_count(&boot2));
    ASSERT_EQ(0, (int)mboot_crash_count(&boot2));  /* clean → reset crash counter */
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Tests: Crash (no clean shutdown)
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(test_crash_increments_counter) {
    reset_all();
    mboot_init(&boot, &test_io, mock_clock);
    mboot_start(&boot);

    /* No shutdown — simulate crash */
    /* Second boot */
    mboot_t boot2;
    mboot_init(&boot2, &test_io, mock_clock);
    mboot_start(&boot2);

    ASSERT_EQ(1, (int)mboot_crash_count(&boot2));
    ASSERT_FALSE(mboot_is_crash_loop(&boot2));
}

TEST(test_crash_loop_detection) {
    reset_all();

    /* Boot and "crash" MBOOT_CRASH_LOOP_THRESHOLD times */
    for (int i = 0; i < MBOOT_CRASH_LOOP_THRESHOLD; i++) {
        mboot_t b;
        mboot_init(&b, &test_io, mock_clock);
        mboot_start(&b);
        /* No shutdown → crash */
        mock_time += 1000;
    }

    /* Next boot should detect crash loop */
    mboot_t final;
    mboot_init(&final, &test_io, mock_clock);
    mboot_start(&final);

    ASSERT_TRUE(mboot_is_crash_loop(&final));
    ASSERT_EQ(MBOOT_MODE_RECOVERY, mboot_mode(&final));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Tests: Crash dump detection
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(test_crash_dump_sets_reason) {
    reset_all();
    mock_has_crash_flag = true;

    mboot_init(&boot, &test_io, mock_clock);
    mboot_start(&boot);

    ASSERT_EQ(MBOOT_REASON_CRASH, mboot_reason(&boot));

    mboot_info_t info;
    mboot_get_info(&boot, &info);
    ASSERT_TRUE(info.has_crash_dump);
}

TEST(test_no_crash_dump) {
    reset_all();
    mock_has_crash_flag = false;

    mboot_init(&boot, &test_io, mock_clock);
    mboot_start(&boot);

    mboot_info_t info;
    mboot_get_info(&boot, &info);
    ASSERT_FALSE(info.has_crash_dump);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Tests: Hardware boot reason
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(test_hw_reason_watchdog) {
    reset_all();
    mock_hw_reason = MBOOT_REASON_WATCHDOG;

    mboot_init(&boot, &test_io, mock_clock);
    mboot_start(&boot);

    ASSERT_EQ(MBOOT_REASON_WATCHDOG, mboot_reason(&boot));
    ASSERT_EQ(1, (int)mboot_crash_count(&boot));
}

TEST(test_hw_reason_brownout) {
    reset_all();
    mock_hw_reason = MBOOT_REASON_BROWNOUT;

    mboot_init(&boot, &test_io, mock_clock);
    mboot_start(&boot);

    ASSERT_EQ(MBOOT_REASON_BROWNOUT, mboot_reason(&boot));
}

TEST(test_hw_reason_user) {
    reset_all();
    mock_hw_reason = MBOOT_REASON_USER;

    mboot_init(&boot, &test_io, mock_clock);
    mboot_start(&boot);

    ASSERT_EQ(MBOOT_REASON_USER, mboot_reason(&boot));
}

TEST(test_no_detect_reason_fn) {
    reset_all();
    mboot_io_t io = test_io;
    io.detect_reason = NULL;

    mboot_init(&boot, &io, mock_clock);
    mboot_start(&boot);

    ASSERT_EQ(MBOOT_REASON_COLD, mboot_reason(&boot));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Tests: Custom decision callback
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(test_custom_decide) {
    reset_all();
    custom_decide_result = MBOOT_MODE_SAFE;

    mboot_init(&boot, &test_io, mock_clock);
    mboot_set_decide(&boot, custom_decide, NULL);
    mboot_start(&boot);

    ASSERT_EQ(1, decide_call_count);
    ASSERT_EQ(MBOOT_MODE_SAFE, mboot_mode(&boot));
}

TEST(test_custom_decide_factory_on_crash_loop) {
    reset_all();

    /* Create crash loop */
    for (int i = 0; i < MBOOT_CRASH_LOOP_THRESHOLD; i++) {
        mboot_t b;
        mboot_init(&b, &test_io, mock_clock);
        mboot_start(&b);
        mock_time += 1000;
    }

    /* Custom decide: crash loop → FACTORY instead of RECOVERY */
    custom_decide_result = MBOOT_MODE_FACTORY;

    mboot_t final;
    mboot_init(&final, &test_io, mock_clock);
    mboot_set_decide(&final, custom_decide, NULL);
    mboot_start(&final);

    ASSERT_EQ(MBOOT_MODE_FACTORY, mboot_mode(&final));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Tests: Confirm (reset crash counter)
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(test_confirm_resets_crash_counter) {
    reset_all();
    mboot_init(&boot, &test_io, mock_clock);
    mboot_start(&boot);
    /* No shutdown → simulates crash */

    mboot_t boot2;
    mboot_init(&boot2, &test_io, mock_clock);
    mboot_start(&boot2);
    ASSERT_EQ(1, (int)mboot_crash_count(&boot2));

    /* App fully initialized — confirm */
    mboot_confirm(&boot2);
    ASSERT_EQ(0, (int)mock_storage.crash_count);

    /* Next boot: crash counter starts from 0 even without shutdown */
    mboot_t boot3;
    mboot_init(&boot3, &test_io, mock_clock);
    mboot_start(&boot3);
    ASSERT_EQ(1, (int)mboot_crash_count(&boot3));  /* one new crash, not 2 */
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Tests: Shutdown records uptime
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(test_shutdown_records_uptime) {
    reset_all();
    mboot_init(&boot, &test_io, mock_clock);
    mboot_start(&boot);

    mock_time += 45000;
    mboot_shutdown(&boot);

    ASSERT_EQ(45000, (int)mock_storage.last_uptime_ms);
    ASSERT_EQ(1, mock_storage.clean_shutdown);
}

TEST(test_uptime_available_next_boot) {
    reset_all();
    mboot_init(&boot, &test_io, mock_clock);
    mboot_start(&boot);

    mock_time += 120000;
    mboot_shutdown(&boot);

    /* Next boot reads previous uptime */
    mock_time = 500;
    mboot_t boot2;
    mboot_init(&boot2, &test_io, mock_clock);
    mboot_start(&boot2);

    mboot_info_t info;
    mboot_get_info(&boot2, &info);
    ASSERT_EQ(120000, (int)info.last_uptime_ms);
    ASSERT_TRUE(info.clean_shutdown);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Tests: IO failures
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(test_read_failure_uses_defaults) {
    reset_all();
    io_fail_read = true;

    mboot_init(&boot, &test_io, mock_clock);
    ASSERT_EQ(MBOOT_OK, mboot_start(&boot));  /* should not fail */
    ASSERT_EQ(MBOOT_MODE_NORMAL, mboot_mode(&boot));
    ASSERT_EQ(1, (int)mboot_boot_count(&boot));
}

TEST(test_write_failure) {
    reset_all();
    io_fail_write = true;

    mboot_init(&boot, &test_io, mock_clock);
    ASSERT_EQ(MBOOT_ERR_IO, mboot_start(&boot));
}

TEST(test_corrupt_record_uses_defaults) {
    reset_all();
    /* Write garbage to storage */
    memset(&mock_storage, 0xAB, sizeof(mock_storage));
    storage_valid = true;

    mboot_init(&boot, &test_io, mock_clock);
    mboot_start(&boot);

    ASSERT_EQ(MBOOT_MODE_NORMAL, mboot_mode(&boot));
    ASSERT_EQ(1, (int)mboot_boot_count(&boot));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Tests: CRC32
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(test_crc32_known_value) {
    /* Standard test vector */
    const char *data = "123456789";
    uint32_t crc = mboot_crc32(data, 9);
    ASSERT_EQ((int)0xCBF43926U, (int)crc);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Tests: Full lifecycle scenario
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(test_full_lifecycle) {
    reset_all();

    /* Boot 1: first boot */
    mboot_t b1;
    mboot_init(&b1, &test_io, mock_clock);
    mboot_start(&b1);
    ASSERT_EQ(MBOOT_REASON_COLD, mboot_reason(&b1));
    ASSERT_EQ(MBOOT_MODE_NORMAL, mboot_mode(&b1));
    ASSERT_EQ(1, (int)mboot_boot_count(&b1));
    mboot_confirm(&b1);
    mock_time += 60000;
    mboot_shutdown(&b1);

    /* Boot 2: clean reboot */
    mock_time = 1000;
    mboot_t b2;
    mboot_init(&b2, &test_io, mock_clock);
    mboot_start(&b2);
    ASSERT_EQ(2, (int)mboot_boot_count(&b2));
    ASSERT_EQ(0, (int)mboot_crash_count(&b2));

    /* Boot 2 crashes (no shutdown, no confirm) */
    mock_time += 5000;

    /* Boot 3: crash */
    mboot_t b3;
    mboot_init(&b3, &test_io, mock_clock);
    mboot_start(&b3);
    ASSERT_EQ(3, (int)mboot_boot_count(&b3));
    ASSERT_EQ(1, (int)mboot_crash_count(&b3));
    ASSERT_FALSE(mboot_is_crash_loop(&b3));

    /* Boot 3 crashes again */
    mock_time += 1000;

    /* Boot 4: second crash */
    mboot_t b4;
    mboot_init(&b4, &test_io, mock_clock);
    mboot_start(&b4);
    ASSERT_EQ(2, (int)mboot_crash_count(&b4));

    /* Boot 4 crashes */
    mock_time += 1000;

    /* Boot 5: third crash → crash loop! */
    mboot_t b5;
    mboot_init(&b5, &test_io, mock_clock);
    mboot_start(&b5);
    ASSERT_EQ(3, (int)mboot_crash_count(&b5));
    ASSERT_TRUE(mboot_is_crash_loop(&b5));
    ASSERT_EQ(MBOOT_MODE_RECOVERY, mboot_mode(&b5));

    /* In recovery mode, confirm breaks the loop */
    mboot_confirm(&b5);
    ASSERT_EQ(0, (int)mboot_crash_count(&b5));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Tests: Edge cases and strings
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(test_query_null) {
    ASSERT_EQ(MBOOT_MODE_NORMAL, mboot_mode(NULL));
    ASSERT_EQ(MBOOT_REASON_COLD, mboot_reason(NULL));
    ASSERT_EQ(0, (int)mboot_boot_count(NULL));
    ASSERT_EQ(0, (int)mboot_crash_count(NULL));
    ASSERT_FALSE(mboot_is_crash_loop(NULL));
}

TEST(test_start_null) {
    ASSERT_EQ(MBOOT_ERR_NULL, mboot_start(NULL));
}

TEST(test_shutdown_null) {
    ASSERT_EQ(MBOOT_ERR_NULL, mboot_shutdown(NULL));
}

TEST(test_confirm_null) {
    ASSERT_EQ(MBOOT_ERR_NULL, mboot_confirm(NULL));
}

TEST(test_get_info_null) {
    mboot_info_t info;
    ASSERT_EQ(MBOOT_ERR_NULL, mboot_get_info(NULL, &info));
    reset_all();
    mboot_init(&boot, &test_io, mock_clock);
    ASSERT_EQ(MBOOT_ERR_NULL, mboot_get_info(&boot, NULL));
}

TEST(test_err_str) {
    ASSERT_STR_EQ("ok",             mboot_err_str(MBOOT_OK));
    ASSERT_STR_EQ("null pointer",   mboot_err_str(MBOOT_ERR_NULL));
    ASSERT_STR_EQ("io error",       mboot_err_str(MBOOT_ERR_IO));
    ASSERT_STR_EQ("record corrupt", mboot_err_str(MBOOT_ERR_CORRUPT));
    ASSERT_STR_EQ("unknown error",  mboot_err_str((mboot_err_t)99));
}

TEST(test_reason_str) {
    ASSERT_STR_EQ("COLD",     mboot_reason_str(MBOOT_REASON_COLD));
    ASSERT_STR_EQ("WATCHDOG", mboot_reason_str(MBOOT_REASON_WATCHDOG));
    ASSERT_STR_EQ("CRASH",    mboot_reason_str(MBOOT_REASON_CRASH));
    ASSERT_STR_EQ("USER",     mboot_reason_str(MBOOT_REASON_USER));
}

TEST(test_mode_str) {
    ASSERT_STR_EQ("NORMAL",   mboot_mode_str(MBOOT_MODE_NORMAL));
    ASSERT_STR_EQ("RECOVERY", mboot_mode_str(MBOOT_MODE_RECOVERY));
    ASSERT_STR_EQ("SAFE",     mboot_mode_str(MBOOT_MODE_SAFE));
    ASSERT_STR_EQ("FACTORY",  mboot_mode_str(MBOOT_MODE_FACTORY));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    printf("\n=== microboot test suite ===\n\n");

    printf("[Init]\n");
    RUN_TEST(test_init);
    RUN_TEST(test_init_null);
    RUN_TEST(test_init_null_io_callbacks);

    printf("\n[First Boot]\n");
    RUN_TEST(test_first_boot);
    RUN_TEST(test_first_boot_writes_record);

    printf("\n[Clean Reboot]\n");
    RUN_TEST(test_clean_reboot);

    printf("\n[Crash Detection]\n");
    RUN_TEST(test_crash_increments_counter);
    RUN_TEST(test_crash_loop_detection);

    printf("\n[Crash Dump]\n");
    RUN_TEST(test_crash_dump_sets_reason);
    RUN_TEST(test_no_crash_dump);

    printf("\n[HW Boot Reason]\n");
    RUN_TEST(test_hw_reason_watchdog);
    RUN_TEST(test_hw_reason_brownout);
    RUN_TEST(test_hw_reason_user);
    RUN_TEST(test_no_detect_reason_fn);

    printf("\n[Custom Decision]\n");
    RUN_TEST(test_custom_decide);
    RUN_TEST(test_custom_decide_factory_on_crash_loop);

    printf("\n[Confirm]\n");
    RUN_TEST(test_confirm_resets_crash_counter);

    printf("\n[Shutdown]\n");
    RUN_TEST(test_shutdown_records_uptime);
    RUN_TEST(test_uptime_available_next_boot);

    printf("\n[IO Failures]\n");
    RUN_TEST(test_read_failure_uses_defaults);
    RUN_TEST(test_write_failure);
    RUN_TEST(test_corrupt_record_uses_defaults);

    printf("\n[CRC32]\n");
    RUN_TEST(test_crc32_known_value);

    printf("\n[Full Lifecycle]\n");
    RUN_TEST(test_full_lifecycle);

    printf("\n[Edge Cases]\n");
    RUN_TEST(test_query_null);
    RUN_TEST(test_start_null);
    RUN_TEST(test_shutdown_null);
    RUN_TEST(test_confirm_null);
    RUN_TEST(test_get_info_null);
    RUN_TEST(test_err_str);
    RUN_TEST(test_reason_str);
    RUN_TEST(test_mode_str);

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n\n");

    return tests_failed > 0 ? 1 : 0;
}
