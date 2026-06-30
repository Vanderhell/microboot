#include "mboot.h"
#include "mboot.h"

int main(void)
{
    mboot_config_t config = mboot_default_config();

    return config.crash_loop_threshold == MBOOT_DEFAULT_CRASH_LOOP_THRESHOLD ? 0 : 1;
}
