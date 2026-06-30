#include "mboot.h"

uint32_t mboot_multi_tu_helper_crash_count(const mboot_t *boot);

uint32_t mboot_multi_tu_helper_crash_count(const mboot_t *boot)
{
    return mboot_crash_count(boot);
}
