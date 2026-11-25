/*
 * examples/minimal_hello/mod_v2.c - Logic hot-swap (v2: +100 per cycle, 0-copy)
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
    s->counter += 100; /* Updated logic! */
    printf("[v2] counter = %d\n", s->counter);
}

BP_EXPORT_MODULE(0, "hello_v2", 0x1001ULL, HelloState, hello_init, hello_exec);
