#include "mboot.h"

#include <cstring>

struct Backend {
    mboot_wire_t slots[2];
    bool present[2];
};

static uint32_t cpp_clock()
{
    return 42u;
}

static mboot_slot_io_result_t cpp_read(uint8_t slot, mboot_wire_t *out, void *ctx)
{
    Backend *backend = static_cast<Backend *>(ctx);
    if (!backend->present[slot]) {
        return MBOOT_SLOT_IO_EMPTY;
    }
    *out = backend->slots[slot];
    return MBOOT_SLOT_IO_OK;
}

static mboot_slot_io_result_t cpp_write(uint8_t slot, const mboot_wire_t *in, void *ctx)
{
    Backend *backend = static_cast<Backend *>(ctx);
    backend->slots[slot] = *in;
    backend->present[slot] = true;
    return MBOOT_SLOT_IO_OK;
}

static mboot_err_t cpp_has_crash(bool *has_crash, void *ctx)
{
    (void)ctx;
    *has_crash = false;
    return MBOOT_OK;
}

static mboot_reason_t cpp_reason(void *ctx)
{
    (void)ctx;
    return MBOOT_REASON_COLD;
}

int main()
{
    Backend backend;
    mboot_io_t io = { cpp_read, cpp_write, cpp_has_crash, cpp_reason, &backend };
    mboot_config_t config = mboot_default_config();
    mboot_t boot;

    std::memset(&backend, 0, sizeof(backend));
    if (mboot_init(&boot, &io, cpp_clock, &config) != MBOOT_OK) {
        return 1;
    }
    if (mboot_start(&boot) != MBOOT_OK) {
        return 2;
    }
    if (mboot_state(&boot) != MBOOT_STATE_STARTED_UNCONFIRMED) {
        return 3;
    }
    return 0;
}
