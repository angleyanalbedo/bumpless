/*
 * fn_motor_v4_crash.c - Faulty update to verify automatic rollback to v3
 */
#include "bumpless.h"
#include "common_ids.h"
#include <stddef.h>

typedef struct {
    float   temperature;
    int32_t speed;
    int32_t torque;
    bool    running;
    void (*calc_vfn)(void* self);
    char*   active_tag;
    char    tag_buf[16];
} FbMotor_v4;

static void motor_init(void* self) {
    (void)self;
}

static void motor_exec(BpRuntimeCtx* ctx, void* self, uint64_t now_ns) {
    (void)ctx; (void)self; (void)now_ns;
    volatile int* p = (volatile int*)NULL;
    *p = 1; /* Trigger Access Violation / SIGSEGV */
}

static const BpModuleABI g_abi = {
    .abi_version      = BUMPLESS_ABI_VERSION,
    .build_tag        = "fn_motor_v4_crash",
    .fn_id            = FN_FB_MOTOR,
    .layout_hash      = 0xA0000003ULL,
    .inst_size        = sizeof(FbMotor_v4),
    .init_inst        = motor_init,
    .exec             = motor_exec,
    .get_fields       = NULL,
    .precompiled_plan = NULL
};

BP_EXPORT const BpModuleABI* bp_get_module_abi(void) {
    return &g_abi;
}
