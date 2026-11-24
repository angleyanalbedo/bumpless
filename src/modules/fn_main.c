/*
 * fn_main.c - MAIN program (NEVER recompiled during FB_Motor hot-swaps!)
 */
#include "bumpless.h"
#include "common_ids.h"
#include <stdio.h>

typedef struct {
    int32_t  cycle_count;
    uint32_t motor1_inst_id; /* Indirect slot ID -> decoupled from FB_Motor layout */
} MainState;

static void main_init(void* self) {
    MainState* s = (MainState*)self;
    s->cycle_count    = 0;
    s->motor1_inst_id = INST_MOTOR1;
}

static void main_exec(BpRuntimeCtx* ctx, void* self, uint64_t now_ns) {
    MainState* s = (MainState*)self;
    s->cycle_count++;
    printf("[MAIN cycle=%d] ", s->cycle_count);
    /* Indirect call through fn_table & inst_table */
    BP_CALL(ctx, FN_FB_MOTOR, s->motor1_inst_id, now_ns);
}

/*
 * Zero-bloat raw POD: No runtime get_fields string metadata.
 */
static const BpModuleABI g_main_abi = {
    .abi_version      = BUMPLESS_ABI_VERSION,
    .build_tag        = "fn_main_v1",
    .fn_id            = FN_MAIN,
    .layout_hash      = 0x10000001ULL,
    .inst_size        = sizeof(MainState),
    .init_inst        = main_init,
    .exec             = main_exec,
    .get_fields       = NULL, /* Raw POD: Zero metadata bloat */
    .precompiled_plan = NULL
};

BP_EXPORT const BpModuleABI* bp_get_module_abi(void) {
    return &g_main_abi;
}
