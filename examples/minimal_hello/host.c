/*
 * examples/minimal_hello/host.c - 30-line minimal host demonstrating live swap
 */
#define BUMPLESS_HOST_IMPL
#include "bumpless.h"
#include <stdio.h>

int main(void) {
    BpHost host;
    bp_host_init(&host, 0);

    bp_host_stage_func(&host, "build/examples/libmod_v1" BP_SO_EXT);
    bp_host_bind_instance(&host, 0, 0);

    printf("--- Running v1 ---\n");
    for (int i = 0; i < 2; i++) bp_host_rt_cycle(&host, 0, 0, bp_monotonic_ns());

    printf("--- Hot-swapping to v2 (0-copy) ---\n");
    bp_host_stage_func(&host, "build/examples/libmod_v2" BP_SO_EXT);
    for (int i = 0; i < 2; i++) bp_host_rt_cycle(&host, 0, 0, bp_monotonic_ns());

    bp_host_shutdown(&host);
    return 0;
}
