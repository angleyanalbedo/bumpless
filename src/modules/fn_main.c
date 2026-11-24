/*
 * fn_main.c - MAIN program (NEVER recompiled during FB_Motor hot-swaps!)
 *
 * Demonstrates Scheme 1 (Native C Transparent Call Wrappers):
 * Uses `fb_motor_call(ctx, s->motor1_inst_id, now_ns)` instead of manual BP_CALL!
 */
#include "bumpless.h"
#include "common_ids.h"
#include "bp_stubs.h"
#include "bp_module.h"
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

    /*
     * Scheme 1 (透明内联桩 / Inline Wrapper Stub):
     * Native C function call syntax! Zero manual BP_CALL or fn_table plumbing!
     */
    fb_motor_call(ctx, s->motor1_inst_id, now_ns);
}

/*
 * Zero-boilerplate module export macro
 */
BP_EXPORT_MODULE(
    FN_MAIN,
    "fn_main_v1",
    0x10000001ULL,
    MainState,
    main_init,
    main_exec
);
