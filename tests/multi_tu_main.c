#include "mboot.h"

#include <stdbool.h>
#include <string.h>

uint32_t mboot_multi_tu_helper_crash_count(const mboot_t *boot);

typedef struct {
    mboot_wire_t slots[2];
    bool present[2];
} multi_backend_t;

static uint32_t multi_clock(void)
{
    return 1u;
}

static mboot_slot_io_result_t multi_read(uint8_t slot, mboot_wire_t *out, void *ctx)
{
    multi_backend_t *backend = (multi_backend_t *)ctx;
    if (!backend->present[slot]) {
        return MBOOT_SLOT_IO_EMPTY;
    }
    *out = backend->slots[slot];
    return MBOOT_SLOT_IO_OK;
}

static mboot_slot_io_result_t multi_write(uint8_t slot, const mboot_wire_t *in, void *ctx)
{
    multi_backend_t *backend = (multi_backend_t *)ctx;
    backend->slots[slot] = *in;
    backend->present[slot] = true;
    return MBOOT_SLOT_IO_OK;
}

static mboot_err_t multi_has_crash(bool *has_crash, void *ctx)
{
    (void)ctx;
    *has_crash = false;
    return MBOOT_OK;
}

static mboot_reason_t multi_reason(void *ctx)
{
    (void)ctx;
    return MBOOT_REASON_COLD;
}

int main(void)
{
    multi_backend_t backend;
    mboot_io_t io = { multi_read, multi_write, multi_has_crash, multi_reason, &backend };
    mboot_config_t config = mboot_default_config();
    mboot_t boot;

    memset(&backend, 0, sizeof(backend));
    if (mboot_init(&boot, &io, multi_clock, &config) != MBOOT_OK) {
        return 1;
    }
    if (mboot_start(&boot) != MBOOT_OK) {
        return 2;
    }
    if (mboot_multi_tu_helper_crash_count(&boot) != 0u) {
        return 3;
    }
    return 0;
}
