/*
 * fn_motor_v3_layout.c - Layout Path (Precompiled CopyPlan with vptr/pointer safety):
 *   - Inserted `temperature` at offset 0.
 *   - `speed` and `torque` remain contiguous -> coalesced into ONE 8-byte memcpy!
 *   - Explicitly preserves new `calc_vfn` (vptr) and `active_tag` (internal pointer).
 *   - Raw POD zero bloat: no get_fields in runtime binary.
 */
#include "bumpless.h"
#include "common_ids.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    float   temperature;          /* NEW field inserted at offset 0 */
    int32_t speed;                /* contiguous with torque -> coalesced into 1 memcpy */
    int32_t torque;
    bool    running;
    void (*calc_vfn)(void* self); /* vptr: MUST NOT BE OVERWRITTEN BY V1/V2 */
    char*   active_tag;           /* internal pointer: MUST NOT POINT TO OLD INSTANCE */
    char    tag_buf[16];
} FbMotor_v3;

static void v3_calc_output(void* self) {
    FbMotor_v3* m = (FbMotor_v3*)self;
    printf("[v3_vfn_NEW: tag=%s, self=%p] ", m->active_tag, self);
}

static void motor_init(void* self) {
    FbMotor_v3* m = (FbMotor_v3*)self;
    m->temperature = 30.0f;
    m->speed       = 0;
    m->torque      = 0;
    m->running     = false;
    m->calc_vfn    = v3_calc_output;
    snprintf(m->tag_buf, sizeof(m->tag_buf), "TAG_V3");
    m->active_tag  = &m->tag_buf[0]; /* Anchored to new instance buffer */
}

static void motor_exec(BpRuntimeCtx* ctx, void* self, uint64_t now_ns) {
    (void)now_ns;
    FbMotor_v3* m = (FbMotor_v3*)self;
    if (m->running) {
        m->speed       += 100;
        m->torque      += 5;
        m->temperature += 0.5f;
    }
    ctx->io->ao[0] = m->speed;

    printf("FB_Motor_v3 (layout migrated): running=%d speed=%d torque=%d temp=%.1f ",
           m->running, m->speed, m->torque, m->temperature);
    m->calc_vfn(self);
    printf("\n");
}

static const BpModuleABI g_abi = {
    .abi_version      = BUMPLESS_ABI_VERSION,
    .build_tag        = "fn_motor_v3_layout",
    .fn_id            = FN_FB_MOTOR,
    .layout_hash      = 0xA0000003ULL, /* Layout changed -> loads precompiled CopyPlan */
    .inst_size        = sizeof(FbMotor_v3),
    .init_inst        = motor_init,
    .exec             = motor_exec,
    .get_fields       = NULL,          /* Raw POD: Zero metadata bloat */
    .precompiled_plan = NULL
};

BP_EXPORT const BpModuleABI* bp_get_module_abi(void) {
    return &g_abi;
}
