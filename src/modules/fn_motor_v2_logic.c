/*
 * fn_motor_v2_logic.c - Fast Path (0-copy):
 *   Same struct layout (layout_hash unchanged), logic updated (+50 speed).
 *   Zero state copy, zero machine-code patching.
 */
#include "bumpless.h"
#include "common_ids.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    int32_t speed;
    int32_t torque;
    bool    running;
    void (*calc_vfn)(void* self);
    char*   active_tag;
    char    tag_buf[16];
} FbMotor_v2;

static void v2_calc_output(void* self) {
    FbMotor_v2* m = (FbMotor_v2*)self;
    printf("[v2_vfn: tag=%s, self=%p] ", m->active_tag, self);
}

static void motor_init(void* self) {
    FbMotor_v2* m = (FbMotor_v2*)self;
    m->speed      = 0;
    m->torque     = 0;
    m->running    = false;
    m->calc_vfn   = v2_calc_output;
    snprintf(m->tag_buf, sizeof(m->tag_buf), "TAG_V2");
    m->active_tag = &m->tag_buf[0];
}

static void motor_exec(BpRuntimeCtx* ctx, void* self, uint64_t now_ns) {
    (void)now_ns;
    FbMotor_v2* m = (FbMotor_v2*)self;
    if (m->running) {
        m->speed  += 50; /* Logic updated */
        m->torque += 2;
    }
    ctx->io->ao[0] = m->speed;

    printf("FB_Motor_v2 (0-copy fast path): running=%d speed=%d torque=%d ",
           m->running, m->speed, m->torque);
    m->calc_vfn(self);
    printf("\n");
}

static const BpModuleABI g_abi = {
    .abi_version      = BUMPLESS_ABI_VERSION,
    .build_tag        = "fn_motor_v2_logic",
    .fn_id            = FN_FB_MOTOR,
    .layout_hash      = 0xA0000001ULL, /* Identical hash -> 0-copy pointer swap */
    .inst_size        = sizeof(FbMotor_v2),
    .init_inst        = motor_init,
    .exec             = motor_exec,
    .get_fields       = NULL,          /* Raw POD: Zero metadata bloat */
    .precompiled_plan = NULL
};

BP_EXPORT const BpModuleABI* bp_get_module_abi(void) {
    return &g_abi;
}
