/*
 * fn_motor_v1.c - Initial FB_Motor implementation
 *
 * Characteristics:
 *   - Raw POD state struct with ZERO runtime reflection bloat (get_fields = NULL).
 *   - Includes simulated vptr (calc_vfn) and internal buffer pointer (active_tag).
 */
#include "bumpless.h"
#include "common_ids.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    int32_t speed;
    int32_t torque;
    bool    running;
    void (*calc_vfn)(void* self); /* Simulated C++ vptr */
    char*   active_tag;           /* Internal self-pointer to tag_buf */
    char    tag_buf[16];
} FbMotor_v1;

static void v1_calc_output(void* self) {
    FbMotor_v1* m = (FbMotor_v1*)self;
    printf("[v1_vfn: tag=%s, self=%p] ", m->active_tag, self);
}

static void motor_init(void* self) {
    FbMotor_v1* m = (FbMotor_v1*)self;
    m->speed      = 100;
    m->torque     = 20;
    m->running    = true;
    m->calc_vfn   = v1_calc_output;
    snprintf(m->tag_buf, sizeof(m->tag_buf), "TAG_V1");
    m->active_tag = &m->tag_buf[0]; /* Points to internal buffer */
}

static void motor_exec(BpRuntimeCtx* ctx, void* self, uint64_t now_ns) {
    (void)now_ns;
    FbMotor_v1* m = (FbMotor_v1*)self;
    if (m->running) {
        m->speed  += 10;
        m->torque += 1;
    }
    ctx->io->ao[0] = m->speed;

    printf("FB_Motor_v1: running=%d speed=%d torque=%d ",
           m->running, m->speed, m->torque);
    m->calc_vfn(self);
    printf("\n");
}

static const BpModuleABI g_abi = {
    .abi_version      = BUMPLESS_ABI_VERSION,
    .build_tag        = "fn_motor_v1",
    .fn_id            = FN_FB_MOTOR,
    .layout_hash      = 0xA0000001ULL,
    .inst_size        = sizeof(FbMotor_v1),
    .init_inst        = motor_init,
    .exec             = motor_exec,
    .get_fields       = NULL, /* Raw POD: Zero metadata bloat */
    .precompiled_plan = NULL
};

BP_EXPORT const BpModuleABI* bp_get_module_abi(void) {
    return &g_abi;
}
