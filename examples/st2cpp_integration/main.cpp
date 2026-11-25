/*
 * examples/st2cpp_integration/main.cpp
 *
 * Demonstrates integrating ST2CPP/runtime-flat's scanOnce() & TCI ProcessImage
 * with Bumpless online hot-update across both logic-only (0-copy) and layout
 * migration (Precompiled CopyPlan) phases.
 */
#include "st2cpp_bumpless_adapter.hpp"
#include "common_ids.h"
#include <cstdio>

#if defined(_WIN32) || defined(__CYGWIN__)
  #define LIB_EXT ".dll"
#else
  #define LIB_EXT ".so"
#endif

/* Simulated Target Communication Interface (like ST2CPP's NetworkTCI / STM32 GPIO) */
class SimFieldbusTCI : public st2cpp_demo::MockTCI {
public:
    void syncInputs(BpIoImage* io) override {
        io->di[0] = 1; /* %IX0.0 = TRUE (Run Command) */
    }
    void syncOutputs(const BpIoImage* io) override {
        std::printf("  [TCI syncOutputs] %%QW0 (Motor Speed) = %d\n", io->ao[0]);
    }
};

int main() {
    std::printf("====================================================================\n");
    std::printf("   ST2CPP (runtime-flat) + Bumpless Online Change Integration Demo\n");
    std::printf("====================================================================\n");

    st2cpp_demo::BumplessRuntimeAdapter rt(4096);
    SimFieldbusTCI fieldbus;
    rt.setTCI(&fieldbus);

    if (rt.loadInitialModule("build/libfn_main" LIB_EXT) != BP_OK) return 1;
    if (rt.loadInitialModule("build/libfn_motor_v1" LIB_EXT) != BP_OK) return 1;
    rt.bindInstance(INST_MAIN, FN_MAIN);
    rt.bindInstance(INST_MOTOR1, FN_FB_MOTOR);

    std::printf("\n[Cycle 1-2] Initial ST2CPP scanOnce() with FB_Motor v1:\n");
    for (int i = 0; i < 2; i++) rt.scanOnce(FN_MAIN, INST_MAIN);

    std::printf("\n[Cycle 3-4] Online Change -> FB_Motor v2 (0-Copy Logic Update):\n");
    rt.stageOnlineChange("build/libfn_motor_v2_logic" LIB_EXT);
    for (int i = 0; i < 2; i++) rt.scanOnce(FN_MAIN, INST_MAIN);

    std::printf("\n[Cycle 5-6] Online Change -> FB_Motor v3 (Layout Migration via CopyPlan):\n");
    rt.stageOnlineChange("build/libfn_motor_v3_layout" LIB_EXT, "build/plan_v1_to_v3.bp_plan");
    for (int i = 0; i < 2; i++) rt.scanOnce(FN_MAIN, INST_MAIN);

    const BpPerfMetrics* m = rt.metrics();
    std::printf("\n[Telemetry] Total swaps=%llu, last_gap=%llu ns, last_copy=%llu ns\n",
                (unsigned long long)m->total_swaps,
                (unsigned long long)m->last_gap_ns,
                (unsigned long long)m->last_copy_ns);
    return 0;
}
