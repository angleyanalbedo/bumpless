/*
 * bumpless.h - Single-header Function-Granularity Real-Time Online Hot-Update Engine
 *
 * Architecture:
 *   1. Function Dispatch Table (g_fn_table[fn_id]):
 *      - Logic-only changes swap a single function pointer at the cycle boundary
 *        with ZERO state copy and ZERO machine-code patching.
 *   2. Flat Instance Slot Pool (g_inst_table[inst_id]):
 *      - Parent POUs reference child FBs by slot ID, so changing a child FB's
 *        struct layout migrates ONLY that FB's slots without cascading to MAIN.
 *   3. Precompiled CopyPlan with Target-ABI Isomorphic Reflection (裸 POD 零膨胀):
 *      - Raw POD structs carry ZERO runtime reflection metadata (no string tables).
 *      - Offline probe / DWARF parser / static reflection computes layout differences
 *        AHEAD OF TIME with exact target ABI rules.
 *      - Explicitly skips C++ vptr and internal pointers to prevent crashes and dangling pointers.
 *      - Contiguous field runs are coalesced into single memcpy operations.
 *      - Real-time cycle gap runs ONLY merged pure memcpy with deterministic execution time.
 *   4. First-Cycle Crash Guard & Automatic Rollback:
 *      - Catches faults on the first cycle after a swap (Windows VEH / POSIX sigaction)
 *        and rolls back both function pointer and instance slot immediately.
 */

#ifndef BUMPLESS_H
#define BUMPLESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
  #define BP_EXPORT __declspec(dllexport)
  #define BP_SO_EXT   ".dll"
#else
  #define BP_EXPORT __attribute__((visibility("default")))
  #define BP_SO_EXT   ".so"
#endif

#define BUMPLESS_ABI_VERSION 2u
#define BUMPLESS_MAX_FUNCS   64u
#define BUMPLESS_MAX_INSTS   128u
#define BUMPLESS_MAX_FORCES  64u

/* Type tags for fields (used in offline probes / optional reflection) */
typedef enum {
    BP_TYPE_UNKNOWN = 0,
    BP_TYPE_BOOL    = 1,
    BP_TYPE_INT8    = 2,
    BP_TYPE_UINT8   = 3,
    BP_TYPE_INT16   = 4,
    BP_TYPE_UINT16  = 5,
    BP_TYPE_INT32   = 6,
    BP_TYPE_UINT32  = 7,
    BP_TYPE_INT64   = 8,
    BP_TYPE_UINT64  = 9,
    BP_TYPE_FLOAT   = 10,
    BP_TYPE_DOUBLE  = 11,
    BP_TYPE_POINTER = 12, /* Raw/internal pointer: MUST BE SKIPPED */
    BP_TYPE_VPTR    = 13  /* Virtual table pointer: MUST BE SKIPPED */
} BpTypeId;

/* Field attribute flags */
typedef enum {
    BP_FLAG_NONE         = 0,
    BP_FLAG_IS_VPTR      = (1 << 0), /* Virtual method table pointer (never copy!) */
    BP_FLAG_IS_POINTER   = (1 << 1), /* Pointer to heap/internal buffer (never copy!) */
    BP_FLAG_INTERNAL_REF = (1 << 2)  /* Points inside same instance */
} BpFieldFlags;

/* Relative-offset leaf descriptor inside a single POU/FB instance layout */
typedef struct {
    const char* field;      /* Field name, e.g. "speed" */
    uint16_t    type_id;    /* BpTypeId */
    uint16_t    flags;      /* BpFieldFlags (BP_FLAG_IS_VPTR / BP_FLAG_IS_POINTER) */
    uint32_t    size;       /* Size in bytes */
    uint32_t    offset;     /* Byte offset in struct (Target ABI compliant) */
} BpFieldLeaf;

typedef struct {
    uint8_t  di[64];
    uint8_t  do_[64];
    int32_t  ai[16];
    int32_t  ao[16];
} BpIoImage;

/* Runtime context passed to every POU/FB function */
typedef struct BpRuntimeCtx BpRuntimeCtx;

typedef void (*BpPouFn)(BpRuntimeCtx* ctx, void* self, uint64_t now_ns);

struct BpRuntimeCtx {
    BpIoImage* io;
    BpPouFn*   fn_table;    /* Indirect function dispatch table */
    void**     inst_table;  /* Flat instance slot pointer table */
};

/* Helper macro for calling another POU/FB through the dispatch table */
#define BP_CALL(ctx, fn_id, inst_id, now_ns) \
    ((ctx)->fn_table[(fn_id)]((ctx), (ctx)->inst_table[(inst_id)], (now_ns)))

/* A single coalesced memory slice copy operation */
typedef struct {
    uint32_t dst_off;       /* Destination byte offset in new instance */
    uint32_t src_off;       /* Source byte offset in old instance */
    uint32_t len;           /* Length in bytes */
} BpCopyOp;

/* Precompiled CopyPlan descriptor */
typedef struct {
    uint32_t        target_fn_id;
    uint64_t        src_layout_hash;
    uint64_t        dst_layout_hash;
    uint32_t        src_inst_size;
    uint32_t        dst_inst_size;
    size_t          copy_op_count;
    size_t          total_copy_bytes;
    const BpCopyOp* copy_ops;
} BpCopyPlan;

#define BP_PLAN_MAGIC   0x4E414C50504D5542ULL /* ASCII: "BUMPPLAN" */
#define BP_PLAN_VERSION 2u

/* File header for serialized .bp_plan binary plans */
#pragma pack(push, 1)
typedef struct {
    uint64_t magic;             /* BP_PLAN_MAGIC */
    uint32_t version;           /* BP_PLAN_VERSION */
    uint32_t target_fn_id;
    uint64_t src_layout_hash;
    uint64_t dst_layout_hash;
    uint32_t src_inst_size;
    uint32_t dst_inst_size;
    uint32_t copy_op_count;
    uint32_t total_copy_bytes;
    uint32_t reserved;
} BpPlanFileHeader;
#pragma pack(pop)

/*
 * ABI exported by each function-level hot-swappable module.
 *
 * NOTE FOR RAW POD (零膨胀):
 * `get_fields` is completely OPTIONAL (can be NULL). Raw POD structs carry zero
 * reflection metadata in the compiled binary. CopyPlans are precompiled offline
 * and either provided in `precompiled_plan` or loaded from `.bp_plan` files.
 */
typedef struct {
    uint32_t          abi_version;
    const char*       build_tag;
    uint32_t          fn_id;          /* Target slot in fn_table */
    uint64_t          layout_hash;    /* Hash of struct layout */
    uint32_t          inst_size;      /* Byte size of one instance */
    void (*init_inst)(void* self);
    BpPouFn           exec;
    const BpFieldLeaf* (*get_fields)(size_t* out_count); /* Optional fallback */
    const BpCopyPlan* precompiled_plan;                  /* Optional precompiled plan */

    /*
     * Reserved Advanced Extension Hooks:
     * 1. Control-Theoretic Bumpless Handover Hook (无冲击平滑过渡):
     *    Invoked immediately after state migration (or logic swap) to perform
     *    integral tracking, rate limiting, or derivative state smoothing so
     *    physical actuator outputs remain C0 continuous without mechanical shock.
     */
    void (*handover_hook)(void* new_self, const void* old_self);

    /*
     * 2. Deep Graph / Dynamic Heap Migration Hook (深层指针图与堆内存深拷贝):
     *    Invoked during state migration for instances containing dynamically allocated
     *    buffers or graph nodes (e.g. dynamic curve arrays float* points).
     */
    void (*custom_migrate_hook)(void* new_self, const void* old_self);
} BpModuleABI;

typedef const BpModuleABI* (*BpGetModuleAbiFn)(void);

typedef enum {
    BP_OK = 0,
    BP_ERR_DLOPEN,
    BP_ERR_SYMBOL,
    BP_ERR_ABI,
    BP_ERR_RANGE,
    BP_ERR_BUDGET_EXCEEDED,
    BP_ERR_ALLOC,
    BP_ERR_PLAN_MISMATCH,
    BP_ERR_PLAN_IO,
    BP_ROLLED_BACK
} BpStatus;

#ifdef BUMPLESS_HOST_IMPL

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(__CYGWIN__)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #include <setjmp.h>
  typedef HMODULE bp_dl_t;
  #define bp_dlopen(p)        LoadLibraryA(p)
  #define bp_dlsym(h, sym)    GetProcAddress((HMODULE)(h), sym)
  #define bp_dlclose(h)       FreeLibrary((HMODULE)(h))
  static const char* bp_dlerror(void) {
      static char err_buf[64];
      snprintf(err_buf, sizeof(err_buf), "Win32 Error: 0x%08lX", GetLastError());
      return err_buf;
  }
#else
  #include <dlfcn.h>
  #include <setjmp.h>
  #include <signal.h>
  #include <time.h>
  typedef void* bp_dl_t;
  #define bp_dlopen(p)        dlopen(p, RTLD_NOW | RTLD_LOCAL)
  #define bp_dlsym(h, sym)    dlsym(h, sym)
  #define bp_dlclose(h)       dlclose(h)
  #define bp_dlerror()        dlerror()
#endif

/* Monotonic time helper (nanoseconds) */
static inline uint64_t bp_monotonic_ns(void) {
#if defined(_WIN32) || defined(__CYGWIN__)
    static LARGE_INTEGER freq;
    static int init = 0;
    if (!init) {
        QueryPerformanceFrequency(&freq);
        init = 1;
    }
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (uint64_t)((counter.QuadPart * 1000000000ULL) / freq.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#endif
}

/* Telemetry metrics for real-time jitter and latency monitoring */
typedef struct {
    uint64_t last_gap_ns;       /* Total cycle gap duration (commit + copy + hook) */
    uint64_t last_copy_ns;      /* Time spent inside coalesced memcpy */
    uint64_t last_exec_ns;      /* Time spent executing MAIN cycle */
    uint64_t last_rollback_ns;  /* Time spent executing rollback if fault occurred */
    uint64_t total_swaps;       /* Total successful swaps committed */
    uint64_t total_rollbacks;   /* Total fault rollbacks executed */
} BpPerfMetrics;

typedef struct {
    bp_dl_t            dl_handle;
    const BpModuleABI* abi;
} BpFuncSlot;

typedef struct {
    uint32_t           owner_fn_id;
    void*              mem;
    uint32_t           size;
    uint64_t           layout_hash;
} BpInstSlot;

typedef struct {
    uint32_t inst_id;
    void*    new_mem;
    void*    old_mem;
} BpInstMigration;

typedef struct {
    uint32_t           fn_id;
    bp_dl_t            new_handle;
    const BpModuleABI* new_abi;
    bool               zero_copy;

    /* Precomputed relative-offset CopyPlan (coalesced contiguous runs). */
    BpCopyOp*          copy_ops;
    size_t             copy_op_count;
    size_t             total_copy_bytes;
    bool               copy_ops_allocated;

    /* Instances belonging to `fn_id` that need slot replacement. */
    BpInstMigration*   migrations;
    size_t             migration_count;
} BpSwapPlan;

typedef struct {
    BpIoImage          io;
    BpPouFn            fn_table[BUMPLESS_MAX_FUNCS];
    void*              inst_table[BUMPLESS_MAX_INSTS];
    BpRuntimeCtx       ctx;

    BpFuncSlot         funcs[BUMPLESS_MAX_FUNCS];
    BpInstSlot         insts[BUMPLESS_MAX_INSTS];

    /* Cycle copy budget in bytes (0 = unlimited). */
    size_t             max_copy_bytes_per_swap;

    /* Rollback snapshot kept until the swapped function survives 1 cycle. */
    bool               verify_pending;
    BpSwapPlan*        active_verify_plan;
    BpFuncSlot         prev_func_slot;

    /* Lock-free handoff between non-RT management thread and RT scan thread. */
    _Atomic(BpSwapPlan*) pending_plan;
    _Atomic(void*)       retired_handle;
    _Atomic(int)         last_status;

    /* Runtime telemetry & performance metrics */
    BpPerfMetrics      metrics;
} BpHost;

/* Thread-local jump buffer for RT crash guard */
#if defined(_WIN32) || defined(__CYGWIN__)
#if defined(_MSC_VER)
static __declspec(thread) jmp_buf g_bp_rt_jmp;
static __declspec(thread) volatile int g_bp_in_cycle = 0;
#else
static __thread jmp_buf g_bp_rt_jmp;
static __thread volatile int g_bp_in_cycle = 0;
#endif
static PVOID g_bp_veh_handle = NULL;

static LONG WINAPI bp_win32_veh_handler(PEXCEPTION_POINTERS ep) {
    if (g_bp_in_cycle) {
        DWORD code = ep->ExceptionRecord->ExceptionCode;
        if (code == EXCEPTION_ACCESS_VIOLATION ||
            code == EXCEPTION_ILLEGAL_INSTRUCTION ||
            code == EXCEPTION_FLT_DIVIDE_BY_ZERO ||
            code == EXCEPTION_INT_DIVIDE_BY_ZERO) {
            g_bp_in_cycle = 0;
            longjmp(g_bp_rt_jmp, (int)code);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#else
static __thread sigjmp_buf g_bp_rt_jmp;
static __thread volatile sig_atomic_t g_bp_in_cycle = 0;

static void bp_rt_fault_handler(int sig) {
    if (g_bp_in_cycle) {
        g_bp_in_cycle = 0;
        siglongjmp(g_bp_rt_jmp, sig);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}
#endif

static void bp_host_init(BpHost* host, size_t max_copy_bytes_per_swap) {
    memset(host, 0, sizeof(*host));
    host->ctx.io         = &host->io;
    host->ctx.fn_table   = host->fn_table;
    host->ctx.inst_table = host->inst_table;
    host->max_copy_bytes_per_swap = max_copy_bytes_per_swap;

    atomic_init(&host->pending_plan, NULL);
    atomic_init(&host->retired_handle, NULL);
    atomic_init(&host->last_status, BP_OK);

#if defined(_WIN32) || defined(__CYGWIN__)
    if (!g_bp_veh_handle) {
        g_bp_veh_handle = AddVectoredExceptionHandler(1, bp_win32_veh_handler);
    }
#else
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = bp_rt_fault_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGFPE,  &sa, NULL);
    sigaction(SIGBUS,  &sa, NULL);
#endif
}

/* Bind an instance slot ID to a POU/FB function ID (initial setup). */
static BpStatus bp_host_bind_instance(BpHost* host, uint32_t inst_id, uint32_t fn_id) {
    if (inst_id >= BUMPLESS_MAX_INSTS || fn_id >= BUMPLESS_MAX_FUNCS) {
        return BP_ERR_RANGE;
    }
    const BpModuleABI* abi = host->funcs[fn_id].abi;
    if (!abi) return BP_ERR_ABI;

    void* mem = calloc(1, abi->inst_size > 0 ? abi->inst_size : 1);
    if (!mem) return BP_ERR_ALLOC;
    if (abi->init_inst) abi->init_inst(mem);

    host->insts[inst_id].owner_fn_id = fn_id;
    host->insts[inst_id].mem         = mem;
    host->insts[inst_id].size        = abi->inst_size;
    host->insts[inst_id].layout_hash = abi->layout_hash;
    host->inst_table[inst_id]        = mem;
    return BP_OK;
}

/* Helper to read serialized .bp_plan binary file */
static BpStatus bp_read_plan_file(const char* filepath,
                                  uint64_t expected_src_hash,
                                  uint64_t expected_dst_hash,
                                  BpCopyOp** out_ops,
                                  size_t* out_count,
                                  size_t* out_bytes) {
    FILE* fp = fopen(filepath, "rb");
    if (!fp) {
        fprintf(stderr, "[bumpless] failed to open plan file '%s'\n", filepath);
        return BP_ERR_PLAN_IO;
    }

    BpPlanFileHeader hdr;
    if (fread(&hdr, sizeof(hdr), 1, fp) != 1) {
        fclose(fp);
        return BP_ERR_PLAN_IO;
    }

    if (hdr.magic != BP_PLAN_MAGIC || hdr.version != BP_PLAN_VERSION) {
        fprintf(stderr, "[bumpless] invalid plan magic/version in '%s'\n", filepath);
        fclose(fp);
        return BP_ERR_PLAN_MISMATCH;
    }

    if (hdr.src_layout_hash != expected_src_hash ||
        hdr.dst_layout_hash != expected_dst_hash) {
        fprintf(stderr, "[bumpless] plan hash mismatch! file=(0x%llx -> 0x%llx) exp=(0x%llx -> 0x%llx)\n",
                (unsigned long long)hdr.src_layout_hash, (unsigned long long)hdr.dst_layout_hash,
                (unsigned long long)expected_src_hash, (unsigned long long)expected_dst_hash);
        fclose(fp);
        return BP_ERR_PLAN_MISMATCH;
    }

    BpCopyOp* ops = NULL;
    if (hdr.copy_op_count > 0) {
        ops = (BpCopyOp*)malloc(hdr.copy_op_count * sizeof(BpCopyOp));
        if (!ops) {
            fclose(fp);
            return BP_ERR_ALLOC;
        }
        if (fread(ops, sizeof(BpCopyOp), hdr.copy_op_count, fp) != hdr.copy_op_count) {
            free(ops);
            fclose(fp);
            return BP_ERR_PLAN_IO;
        }
    }

    fclose(fp);
    *out_ops   = ops;
    *out_count = hdr.copy_op_count;
    *out_bytes = hdr.total_copy_bytes;
    return BP_OK;
}

/*
 * Dynamic Stage-time Field Merging with Strict vptr & Pointer Exclusion:
 * Used when modules provide get_fields (e.g. during development).
 * Explicitly ignores vptr and raw/internal pointers, coalescing contiguous ranges.
 */
static BpStatus bp_build_coalesced_plan(const BpModuleABI* old_abi,
                                        const BpModuleABI* new_abi,
                                        BpCopyOp** out_ops,
                                        size_t* out_count,
                                        size_t* out_bytes) {
    if (!old_abi->get_fields || !new_abi->get_fields) {
        return BP_ERR_ABI;
    }

    size_t old_n = 0, new_n = 0;
    const BpFieldLeaf* old_f = old_abi->get_fields(&old_n);
    const BpFieldLeaf* new_f = new_abi->get_fields(&new_n);

    size_t cap = old_n < new_n ? old_n : new_n;
    BpCopyOp* ops = cap > 0 ? (BpCopyOp*)malloc(cap * sizeof(BpCopyOp)) : NULL;
    if (cap > 0 && !ops) return BP_ERR_ALLOC;

    size_t count = 0;
    size_t total_bytes = 0;
    size_t i = 0, j = 0;

    while (i < old_n && j < new_n) {
        int cmp = strcmp(old_f[i].field, new_f[j].field);
        if (cmp < 0) {
            i++;
        } else if (cmp > 0) {
            j++;
        } else {
            /*
             * CRITICAL REQUIREMENT:
             * Explicitly skip vptr and internal/raw pointers!
             * Vptr must be initialized fresh by new_abi->init_inst to point to
             * the new class's vtable. Overwriting it with old vptr crashes when
             * the old module is unloaded. Internal pointers must be re-anchored.
             */
            bool is_vptr = (old_f[i].flags & BP_FLAG_IS_VPTR) ||
                           (new_f[j].flags & BP_FLAG_IS_VPTR) ||
                           (old_f[i].type_id == BP_TYPE_VPTR) ||
                           (new_f[j].type_id == BP_TYPE_VPTR) ||
                           (strcmp(old_f[i].field, "_vptr") == 0);

            bool is_ptr  = (old_f[i].flags & BP_FLAG_IS_POINTER) ||
                           (new_f[j].flags & BP_FLAG_IS_POINTER) ||
                           (old_f[i].type_id == BP_TYPE_POINTER) ||
                           (new_f[j].type_id == BP_TYPE_POINTER);

            if (!is_vptr && !is_ptr &&
                old_f[i].type_id == new_f[j].type_id &&
                old_f[i].size == new_f[j].size) {
                uint32_t src_off = old_f[i].offset;
                uint32_t dst_off = new_f[j].offset;
                uint32_t len     = old_f[i].size;

                /* Coalesce contiguous memory slices into a single memcpy */
                if (count > 0 &&
                    ops[count - 1].src_off + ops[count - 1].len == src_off &&
                    ops[count - 1].dst_off + ops[count - 1].len == dst_off) {
                    ops[count - 1].len += len;
                } else {
                    ops[count++] = (BpCopyOp){ dst_off, src_off, len };
                }
                total_bytes += len;
            }
            i++;
            j++;
        }
    }

    *out_ops   = ops;
    *out_count = count;
    *out_bytes = total_bytes;
    return BP_OK;
}

static void bp_free_plan(BpSwapPlan* plan, bool free_new_mems, bool free_old_mems) {
    if (!plan) return;
    for (size_t i = 0; i < plan->migration_count; i++) {
        if (free_new_mems && plan->migrations[i].new_mem) {
            free(plan->migrations[i].new_mem);
        }
        if (free_old_mems && plan->migrations[i].old_mem) {
            free(plan->migrations[i].old_mem);
        }
    }
    free(plan->migrations);
    if (plan->copy_ops_allocated && plan->copy_ops) {
        free(plan->copy_ops);
    }
    free(plan);
}

/*
 * NON-REAL-TIME MANAGEMENT THREAD:
 * Stages a function hot-swap using an optional precompiled plan (file or embedded)
 * or dynamic reflection.
 */
static BpStatus bp_host_stage_func_ex(BpHost* host, const char* so_path, const char* plan_file) {
    void* retired = atomic_exchange_explicit(&host->retired_handle, NULL,
                                             memory_order_acq_rel);
    if (retired) bp_dlclose(retired);

    bp_dl_t handle = bp_dlopen(so_path);
    if (!handle) {
        fprintf(stderr, "[bumpless] dlopen(%s) failed: %s\n", so_path, bp_dlerror());
        return BP_ERR_DLOPEN;
    }

    BpGetModuleAbiFn get_abi = (BpGetModuleAbiFn)(void*)(uintptr_t)bp_dlsym(handle, "bp_get_module_abi");
    if (!get_abi) {
        bp_dlclose(handle);
        return BP_ERR_SYMBOL;
    }

    const BpModuleABI* new_abi = get_abi();
    if (!new_abi || new_abi->abi_version != BUMPLESS_ABI_VERSION ||
        new_abi->fn_id >= BUMPLESS_MAX_FUNCS) {
        bp_dlclose(handle);
        return BP_ERR_ABI;
    }

    uint32_t fn_id = new_abi->fn_id;
    const BpModuleABI* old_abi = host->funcs[fn_id].abi;

    /* Initial load before RT loop starts: install directly */
    if (!old_abi) {
        host->funcs[fn_id].dl_handle = handle;
        host->funcs[fn_id].abi       = new_abi;
        host->fn_table[fn_id]        = new_abi->exec;
        return BP_OK;
    }

    BpSwapPlan* plan = (BpSwapPlan*)calloc(1, sizeof(BpSwapPlan));
    if (!plan) {
        bp_dlclose(handle);
        return BP_ERR_ALLOC;
    }
    plan->fn_id      = fn_id;
    plan->new_handle = handle;
    plan->new_abi    = new_abi;

    /* Fast Path: struct layout unchanged -> 0 bytes memcpy! */
    if (old_abi->layout_hash == new_abi->layout_hash &&
        old_abi->inst_size == new_abi->inst_size) {
        plan->zero_copy = true;
        printf("[bumpless] staged '%s' (fn_id=%u): FAST PATH (0-copy pointer swap)\n",
               new_abi->build_tag, fn_id);
        atomic_store_explicit(&host->pending_plan, plan, memory_order_release);
        return BP_OK;
    }

    /*
     * Layout Path: Obtain precompiled CopyPlan.
     * Priority:
     *   1. External precompiled .bp_plan file (if supplied)
     *   2. Embedded precompiled plan in BpModuleABI (precompiled_plan)
     *   3. Dynamic reflection fallback (if get_fields provided)
     */
    BpStatus st = BP_OK;
    if (plan_file && plan_file[0] != '\0') {
        st = bp_read_plan_file(plan_file,
                               old_abi->layout_hash,
                               new_abi->layout_hash,
                               &plan->copy_ops,
                               &plan->copy_op_count,
                               &plan->total_copy_bytes);
        plan->copy_ops_allocated = true;
    } else if (new_abi->precompiled_plan &&
               new_abi->precompiled_plan->src_layout_hash == old_abi->layout_hash &&
               new_abi->precompiled_plan->dst_layout_hash == new_abi->layout_hash) {
        const BpCopyPlan* cp = new_abi->precompiled_plan;
        plan->copy_ops           = (BpCopyOp*)cp->copy_ops;
        plan->copy_op_count      = cp->copy_op_count;
        plan->total_copy_bytes   = cp->total_copy_bytes;
        plan->copy_ops_allocated = false;
        st = BP_OK;
    } else if (old_abi->get_fields && new_abi->get_fields) {
        st = bp_build_coalesced_plan(old_abi, new_abi,
                                      &plan->copy_ops,
                                      &plan->copy_op_count,
                                      &plan->total_copy_bytes);
        plan->copy_ops_allocated = true;
    } else {
        fprintf(stderr, "[bumpless] error: layout changed for '%s', but no precompiled CopyPlan or get_fields found!\n",
                new_abi->build_tag);
        st = BP_ERR_PLAN_MISMATCH;
    }

    if (st != BP_OK) {
        bp_free_plan(plan, false, false);
        bp_dlclose(handle);
        return st;
    }

    /* Collect all instances owned by this fn_id and pre-allocate new slots */
    size_t inst_cnt = 0;
    for (uint32_t id = 0; id < BUMPLESS_MAX_INSTS; id++) {
        if (host->insts[id].mem && host->insts[id].owner_fn_id == fn_id) {
            inst_cnt++;
        }
    }

    size_t total_swap_bytes = plan->total_copy_bytes * inst_cnt;
    if (host->max_copy_bytes_per_swap > 0 &&
        total_swap_bytes > host->max_copy_bytes_per_swap) {
        fprintf(stderr, "[bumpless] REJECTED '%s': copy %zuB exceeds cycle budget %zuB\n",
                new_abi->build_tag, total_swap_bytes, host->max_copy_bytes_per_swap);
        bp_free_plan(plan, false, false);
        bp_dlclose(handle);
        return BP_ERR_BUDGET_EXCEEDED;
    }

    if (inst_cnt > 0) {
        plan->migrations = (BpInstMigration*)calloc(inst_cnt, sizeof(BpInstMigration));
        if (!plan->migrations) {
            bp_free_plan(plan, false, false);
            bp_dlclose(handle);
            return BP_ERR_ALLOC;
        }
        size_t idx = 0;
        for (uint32_t id = 0; id < BUMPLESS_MAX_INSTS; id++) {
            if (host->insts[id].mem && host->insts[id].owner_fn_id == fn_id) {
                void* new_mem = calloc(1, new_abi->inst_size > 0 ? new_abi->inst_size : 1);
                if (!new_mem) {
                    bp_free_plan(plan, true, false);
                    bp_dlclose(handle);
                    return BP_ERR_ALLOC;
                }
                /* Run constructor / initializer on fresh instance slot */
                if (new_abi->init_inst) new_abi->init_inst(new_mem);
                plan->migrations[idx++] = (BpInstMigration){
                    .inst_id = id,
                    .new_mem = new_mem,
                    .old_mem = host->insts[id].mem
                };
            }
        }
        plan->migration_count = idx;
    }

    printf("[bumpless] staged '%s' (fn_id=%u): PRECOMPILED COPYPLAN (%zu insts, %zu coalesced ops, %zuB total)\n",
           new_abi->build_tag, fn_id, plan->migration_count,
           plan->copy_op_count, total_swap_bytes);
    atomic_store_explicit(&host->pending_plan, plan, memory_order_release);
    return BP_OK;
}

static inline BpStatus bp_host_stage_func(BpHost* host, const char* so_path) {
    return bp_host_stage_func_ex(host, so_path, NULL);
}

/*
 * REAL-TIME SCAN THREAD:
 * Cycle boundary commit + crash-guarded execution.
 * Only executes merged pure memcpy calls! Zero string lookups, zero dynamic allocation.
 * Execution time is strictly bounded and predictable.
 */
static void bp_host_rt_cycle(BpHost* host, uint32_t main_fn_id,
                             uint32_t main_inst_id, uint64_t now_ns) {
    uint64_t gap_start_ns = bp_monotonic_ns();

    /* 1. Quiescent point: commit any staged function swap */
    BpSwapPlan* plan = atomic_exchange_explicit(&host->pending_plan, NULL,
                                                memory_order_acq_rel);
    if (plan) {
        /* If previous hot-swap was pending verification, it survived until this cycle. Commit it now! */
        if (host->verify_pending && host->active_verify_plan) {
            host->verify_pending = false;
            void* prev_retired = atomic_exchange_explicit(&host->retired_handle,
                                                          host->prev_func_slot.dl_handle,
                                                          memory_order_acq_rel);
            if (prev_retired) bp_dlclose(prev_retired);
            bp_free_plan(host->active_verify_plan, false, true);
            host->active_verify_plan = NULL;
        }

        uint32_t fid = plan->fn_id;
        host->prev_func_slot = host->funcs[fid];

        if (!plan->zero_copy) {
            uint64_t copy_start_ns = bp_monotonic_ns();
            for (size_t m = 0; m < plan->migration_count; m++) {
                uint8_t* dst = (uint8_t*)plan->migrations[m].new_mem;
                const uint8_t* src = (const uint8_t*)plan->migrations[m].old_mem;

                /* Pure coalesced memcpy execution */
                for (size_t k = 0; k < plan->copy_op_count; k++) {
                    memcpy(dst + plan->copy_ops[k].dst_off,
                           src + plan->copy_ops[k].src_off,
                           plan->copy_ops[k].len);
                }

                /* Deep Graph / Custom Heap Migration Hook */
                if (plan->new_abi->custom_migrate_hook) {
                    plan->new_abi->custom_migrate_hook(dst, src);
                }

                /* Control-Theoretic Bumpless Handover Hook */
                if (plan->new_abi->handover_hook) {
                    plan->new_abi->handover_hook(dst, src);
                }

                uint32_t iid = plan->migrations[m].inst_id;
                host->insts[iid].mem         = dst;
                host->insts[iid].size        = plan->new_abi->inst_size;
                host->insts[iid].layout_hash = plan->new_abi->layout_hash;
                host->inst_table[iid]        = dst;
            }
            host->metrics.last_copy_ns = bp_monotonic_ns() - copy_start_ns;
        } else {
            host->metrics.last_copy_ns = 0;
            /* In 0-copy fast path, invoke handover_hook if provided */
            if (plan->new_abi->handover_hook) {
                for (uint32_t id = 0; id < BUMPLESS_MAX_INSTS; id++) {
                    if (host->insts[id].mem && host->insts[id].owner_fn_id == fid) {
                        plan->new_abi->handover_hook(host->insts[id].mem, host->insts[id].mem);
                    }
                }
            }
        }

        host->funcs[fid].dl_handle = plan->new_handle;
        host->funcs[fid].abi       = plan->new_abi;
        host->fn_table[fid]        = plan->new_abi->exec;

        host->verify_pending     = true;
        host->active_verify_plan = plan;

        host->metrics.last_gap_ns = bp_monotonic_ns() - gap_start_ns;
        host->metrics.total_swaps++;
    } else {
        host->metrics.last_gap_ns = bp_monotonic_ns() - gap_start_ns;
        host->metrics.last_copy_ns = 0;
    }

    /* 2. Run MAIN cycle inside crash guard */
#if defined(_WIN32) || defined(__CYGWIN__)
    int fault_sig = setjmp(g_bp_rt_jmp);
#else
    int fault_sig = sigsetjmp(g_bp_rt_jmp, 1);
#endif

    if (fault_sig == 0) {
        g_bp_in_cycle = 1;
        uint64_t exec_start_ns = bp_monotonic_ns();
        BP_CALL(&host->ctx, main_fn_id, main_inst_id, now_ns);
        host->metrics.last_exec_ns = bp_monotonic_ns() - exec_start_ns;
        g_bp_in_cycle = 0;

        /* New function survived its first cycle: commit permanently */
        if (host->verify_pending) {
            host->verify_pending = false;
            atomic_store_explicit(&host->retired_handle,
                                  host->prev_func_slot.dl_handle,
                                  memory_order_release);
            bp_free_plan(host->active_verify_plan, false, true);
            host->active_verify_plan = NULL;
            atomic_store_explicit(&host->last_status, BP_OK, memory_order_release);
        }
    } else {
        /* Fault caught! Roll back function pointer & instance slots immediately */
        uint64_t rb_start_ns = bp_monotonic_ns();
        BpSwapPlan* bad = host->active_verify_plan;
        if (host->verify_pending && bad) {
            uint32_t fid = bad->fn_id;
            fprintf(stderr, "[bumpless] FAULT (code=0x%X) in '%s' -> ROLLING BACK to '%s'\n",
                    fault_sig, bad->new_abi->build_tag,
                    host->prev_func_slot.abi->build_tag);

            if (!bad->zero_copy) {
                for (size_t m = 0; m < bad->migration_count; m++) {
                    uint32_t iid = bad->migrations[m].inst_id;
                    host->insts[iid].mem         = bad->migrations[m].old_mem;
                    host->insts[iid].size        = host->prev_func_slot.abi->inst_size;
                    host->insts[iid].layout_hash = host->prev_func_slot.abi->layout_hash;
                    host->inst_table[iid]        = bad->migrations[m].old_mem;
                }
            }

            host->funcs[fid]     = host->prev_func_slot;
            host->fn_table[fid]  = host->prev_func_slot.abi->exec;
            host->verify_pending = false;

            atomic_store_explicit(&host->retired_handle, bad->new_handle,
                                  memory_order_release);
            bp_free_plan(bad, true, false);
            host->active_verify_plan = NULL;
            atomic_store_explicit(&host->last_status, BP_ROLLED_BACK,
                                  memory_order_release);

            host->metrics.last_rollback_ns = bp_monotonic_ns() - rb_start_ns;
            host->metrics.total_rollbacks++;

            /* Re-execute cycle with rolled-back known-good function */
            uint64_t exec_start_ns = bp_monotonic_ns();
            BP_CALL(&host->ctx, main_fn_id, main_inst_id, now_ns);
            host->metrics.last_exec_ns = bp_monotonic_ns() - exec_start_ns;
        }
    }
}

static inline const BpPerfMetrics* bp_host_get_metrics(const BpHost* host) {
    return &host->metrics;
}

static void bp_host_shutdown(BpHost* host) {
    void* retired = atomic_exchange(&host->retired_handle, NULL);
    if (retired) bp_dlclose(retired);
    for (uint32_t i = 0; i < BUMPLESS_MAX_INSTS; i++) {
        free(host->insts[i].mem);
        host->insts[i].mem = NULL;
    }
    for (uint32_t i = 0; i < BUMPLESS_MAX_FUNCS; i++) {
        if (host->funcs[i].dl_handle) {
            bp_dlclose(host->funcs[i].dl_handle);
            host->funcs[i].dl_handle = NULL;
        }
    }
#if defined(_WIN32) || defined(__CYGWIN__)
    if (g_bp_veh_handle) {
        RemoveVectoredExceptionHandler(g_bp_veh_handle);
        g_bp_veh_handle = NULL;
    }
#endif
}

#endif /* BUMPLESS_HOST_IMPL */

#ifdef __cplusplus
}
#endif

#endif /* BUMPLESS_H */
