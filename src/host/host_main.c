/*
 * host_main.c - Bumpless Real-Time Hot-Update Host Engine
 *
 * Demonstrates:
 *   1. Zero Runtime Bloat (裸 POD 零膨胀): Modules contain NO runtime get_fields string tables.
 *   2. Fast Path (0-copy): Logic update swaps function pointer only.
 *   3. Layout Path (Precompiled CopyPlan):
 *      - Offline probe extracted target-ABI layout ahead of time.
 *      - Coalesced contiguous fields into minimal memcpy runs.
 *      - Explicitly skipped vptr and internal pointer (preventing crash / dangling pointer).
 *      - RT cycle gap executes pure coalesced memcpy only.
 *   4. First-Cycle Crash Guard: Automatic instant rollback upon fault.
 */

#define BUMPLESS_HOST_IMPL
#include "bumpless.h"
#include "common_ids.h"
#include <stdio.h>

#if defined(_WIN32) || defined(__CYGWIN__)
  #define LIB_EXT    ".dll"
#else
  #define LIB_EXT    ".so"
#endif

static const char* resolve_artifact(const char* base_name, char* out_buf, size_t max_len) {
    FILE* fp = fopen(base_name, "rb");
    if (fp) {
        fclose(fp);
        snprintf(out_buf, max_len, "%s", base_name);
        return out_buf;
    }
    snprintf(out_buf, max_len, "build/%s", base_name);
    fp = fopen(out_buf, "rb");
    if (fp) {
        fclose(fp);
        return out_buf;
    }
    snprintf(out_buf, max_len, "%s", base_name);
    return out_buf;
}

int main(void) {
    printf("====================================================================\n");
    printf("   BUMPLESS: Function-Granularity Real-Time Online Hot-Update\n");
    printf("   Architecture: Precompiled CopyPlan + Target-ABI Isomorphic Probe\n");
    printf("   Key Guarantees: Naked POD Zero Bloat | Pure Coalesced memcpy | vptr Safe\n");
    printf("====================================================================\n\n");

    BpHost host;
    /* Set a 4 KB cycle copy budget guard */
    bp_host_init(&host, 4096);

    char path_main[256];
    char path_motor_v1[256];
    char path_motor_v2[256];
    char path_motor_v3[256];
    char path_motor_v4[256];
    char path_plan[256];

    resolve_artifact("libfn_main" LIB_EXT, path_main, sizeof(path_main));
    resolve_artifact("libfn_motor_v1" LIB_EXT, path_motor_v1, sizeof(path_motor_v1));
    resolve_artifact("libfn_motor_v2_logic" LIB_EXT, path_motor_v2, sizeof(path_motor_v2));
    resolve_artifact("libfn_motor_v3_layout" LIB_EXT, path_motor_v3, sizeof(path_motor_v3));
    resolve_artifact("libfn_motor_v4_crash" LIB_EXT, path_motor_v4, sizeof(path_motor_v4));
    resolve_artifact("plan_v1_to_v3.bp_plan", path_plan, sizeof(path_plan));

    /* Initial load: MAIN + FB_Motor v1 */
    if (bp_host_stage_func(&host, path_main) != BP_OK) return 1;
    if (bp_host_stage_func(&host, path_motor_v1) != BP_OK) return 1;

    if (bp_host_bind_instance(&host, INST_MAIN,   FN_MAIN)     != BP_OK) return 1;
    if (bp_host_bind_instance(&host, INST_MOTOR1, FN_FB_MOTOR) != BP_OK) return 1;

    printf("=== Phase 1: Initial run (MAIN + FB_Motor v1) ===\n");
    for (int i = 0; i < 2; i++) {
        bp_host_rt_cycle(&host, FN_MAIN, INST_MAIN, bp_monotonic_ns());
    }

    printf("\n=== Phase 2: Logic-only hot-swap (FB_Motor v1 -> v2, 0-copy pointer swap!) ===\n");
    if (bp_host_stage_func(&host, path_motor_v2) != BP_OK) return 1;
    for (int i = 0; i < 2; i++) {
        bp_host_rt_cycle(&host, FN_MAIN, INST_MAIN, bp_monotonic_ns());
    }

    printf("\n=== Phase 3: Layout migration (FB_Motor v2 -> v3 via Precompiled CopyPlan) ===\n");
    printf("  [Note] Using offline-generated '%s'\n", path_plan);
    printf("  [Note] vptr & internal pointer explicitly skipped; contiguous runs coalesced.\n");
    if (bp_host_stage_func_ex(&host, path_motor_v3, path_plan) != BP_OK) return 1;
    for (int i = 0; i < 3; i++) {
        bp_host_rt_cycle(&host, FN_MAIN, INST_MAIN, bp_monotonic_ns());
    }

    printf("\n=== Phase 4: Faulty hot-swap (FB_Motor v3 -> v4_crash, auto rollback) ===\n");
    if (bp_host_stage_func_ex(&host, path_motor_v4, path_plan) != BP_OK) return 1;
    for (int i = 0; i < 2; i++) {
        bp_host_rt_cycle(&host, FN_MAIN, INST_MAIN, bp_monotonic_ns());
    }

    printf("\n[bumpless] All hot-update phases completed successfully.\n");
    bp_host_shutdown(&host);
    return 0;
}
