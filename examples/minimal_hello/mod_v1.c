/*
 * examples/minimal_hello/mod_v1.c - Minimal hot-swappable module (v1)
 */
#include "bumpless.h"
#include "bp_module.h"
#include <stdio.h>

typedef struct {
    int32_t counter;
} HelloState;

static void hello_init(void* self) {
    ((HelloState*)self)->counter = 0;
}

static void hello_exec(BpRuntimeCtx* ctx, void* self, uint64_t now_ns) {
    (void)ctx; (void)now_ns;
    HelloState* s = (HelloState*)self;
    s->counter += 1;
    printf("[v1] counter = %d\n", s->counter);
}

BP_EXPORT_MODULE(0, "hello_v1", 0x1001ULL, HelloState, hello_init, hello_exec);
