/*
 * bp_module.h - Helper macros for zero-boilerplate module implementation
 *
 * Allows developers to migrate or write hot-swappable POU/FB modules in
 * standard C without manually constructing BpModuleABI or export boilerplate.
 */

#ifndef BP_MODULE_H
#define BP_MODULE_H

#include "bumpless.h"

/*
 * Macro to declare and export a zero-bloat, production-ready hot-swappable module.
 *
 * Parameters:
 *   FnId:       Function slot ID in g_fn_table (e.g. FN_FB_MOTOR)
 *   BuildTag:   String tag for logging and crash rollback diagnostics
 *   LayoutHash: 64-bit layout hash (from probe or manual seed)
 *   StructType: The naked C POD struct (e.g. FbMotor_v1)
 *   InitFn:     Constructor function: void (*)(void* self)
 *   ExecFn:     Cyclic execution function: void (*)(BpRuntimeCtx* ctx, void* self, uint64_t now_ns)
 */
#define BP_EXPORT_MODULE(FnId, BuildTag, LayoutHash, StructType, InitFn, ExecFn) \
    static const BpModuleABI g_bp_module_abi = {                                 \
        .abi_version      = BUMPLESS_ABI_VERSION,                                \
        .build_tag        = BuildTag,                                            \
        .fn_id            = (uint32_t)(FnId),                                    \
        .layout_hash      = (uint64_t)(LayoutHash),                              \
        .inst_size        = (uint32_t)sizeof(StructType),                        \
        .init_inst        = (void (*)(void*))(InitFn),                           \
        .exec             = (BpPouFn)(ExecFn),                                   \
        .get_fields       = NULL, /* Raw POD: Zero runtime reflection bloat */   \
        .precompiled_plan = NULL                                                 \
    };                                                                           \
    BP_EXPORT const BpModuleABI* bp_get_module_abi(void) {                       \
        return &g_bp_module_abi;                                                 \
    }

#endif /* BP_MODULE_H */
