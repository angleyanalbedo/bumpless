/*
 * tools/probe_motor.c - Target-ABI Isomorphic Offline Probe for FB_Motor
 *
 * Compiled with the target toolchain to produce 100% ABI-identical offsets,
 * alignments, and padding.
 */
#include "bp_probe.h"

/* V1 Struct definition */
typedef struct {
    int32_t speed;
    int32_t torque;
    bool    running;
    void (*calc_vfn)(void* self);
    char*   active_tag;
    char    tag_buf[16];
} FbMotor_v1;

/* V3 Struct definition */
typedef struct {
    float   temperature;
    int32_t speed;
    int32_t torque;
    bool    running;
    void (*calc_vfn)(void* self);
    char*   active_tag;
    char    tag_buf[16];
} FbMotor_v3;

static const BpProbeField g_v1_fields[] = {
    { "speed",       (uint32_t)offsetof(FbMotor_v1, speed),       (uint32_t)sizeof(((FbMotor_v1*)0)->speed),       BP_TYPE_INT32,   false, false },
    { "torque",      (uint32_t)offsetof(FbMotor_v1, torque),      (uint32_t)sizeof(((FbMotor_v1*)0)->torque),      BP_TYPE_INT32,   false, false },
    { "running",     (uint32_t)offsetof(FbMotor_v1, running),     (uint32_t)sizeof(((FbMotor_v1*)0)->running),     BP_TYPE_BOOL,    false, false },
    { "calc_vfn",    (uint32_t)offsetof(FbMotor_v1, calc_vfn),    (uint32_t)sizeof(((FbMotor_v1*)0)->calc_vfn),    BP_TYPE_VPTR,    true,  false }, /* VPTR: SKIP! */
    { "active_tag",  (uint32_t)offsetof(FbMotor_v1, active_tag),  (uint32_t)sizeof(((FbMotor_v1*)0)->active_tag),  BP_TYPE_POINTER, false, true  }, /* PTR: SKIP!  */
    { "tag_buf",     (uint32_t)offsetof(FbMotor_v1, tag_buf),     (uint32_t)sizeof(((FbMotor_v1*)0)->tag_buf),     BP_TYPE_UINT8,   false, false }
};

static const BpProbeField g_v3_fields[] = {
    { "temperature", (uint32_t)offsetof(FbMotor_v3, temperature), (uint32_t)sizeof(((FbMotor_v3*)0)->temperature), BP_TYPE_FLOAT,   false, false },
    { "speed",       (uint32_t)offsetof(FbMotor_v3, speed),       (uint32_t)sizeof(((FbMotor_v3*)0)->speed),       BP_TYPE_INT32,   false, false },
    { "torque",      (uint32_t)offsetof(FbMotor_v3, torque),      (uint32_t)sizeof(((FbMotor_v3*)0)->torque),      BP_TYPE_INT32,   false, false },
    { "running",     (uint32_t)offsetof(FbMotor_v3, running),     (uint32_t)sizeof(((FbMotor_v3*)0)->running),     BP_TYPE_BOOL,    false, false },
    { "calc_vfn",    (uint32_t)offsetof(FbMotor_v3, calc_vfn),    (uint32_t)sizeof(((FbMotor_v3*)0)->calc_vfn),    BP_TYPE_VPTR,    true,  false }, /* VPTR: SKIP! */
    { "active_tag",  (uint32_t)offsetof(FbMotor_v3, active_tag),  (uint32_t)sizeof(((FbMotor_v3*)0)->active_tag),  BP_TYPE_POINTER, false, true  }, /* PTR: SKIP!  */
    { "tag_buf",     (uint32_t)offsetof(FbMotor_v3, tag_buf),     (uint32_t)sizeof(((FbMotor_v3*)0)->tag_buf),     BP_TYPE_UINT8,   false, false }
};

int main(int argc, char** argv) {
    BpProbeStruct s_v1 = {
        .struct_name = "FbMotor_v1",
        .inst_size   = (uint32_t)sizeof(FbMotor_v1),
        .layout_hash = 0xA0000001ULL,
        .field_count = sizeof(g_v1_fields) / sizeof(g_v1_fields[0]),
        .fields      = g_v1_fields
    };

    BpProbeStruct s_v3 = {
        .struct_name = "FbMotor_v3",
        .inst_size   = (uint32_t)sizeof(FbMotor_v3),
        .layout_hash = 0xA0000003ULL,
        .field_count = sizeof(g_v3_fields) / sizeof(g_v3_fields[0]),
        .fields      = g_v3_fields
    };

    const char* out_dir = ".";
    if (argc >= 2) {
        if (strcmp(argv[1], "v1") == 0) {
            bp_probe_dump_json(stdout, &s_v1);
            return 0;
        } else if (strcmp(argv[1], "v3") == 0) {
            bp_probe_dump_json(stdout, &s_v3);
            return 0;
        } else {
            out_dir = argv[1];
        }
    }

    char p1[256], p3[256];
    snprintf(p1, sizeof(p1), "%s/layout_v1.json", out_dir);
    snprintf(p3, sizeof(p3), "%s/layout_v3.json", out_dir);

    FILE* f1 = fopen(p1, "w");
    if (f1) { bp_probe_dump_json(f1, &s_v1); fclose(f1); }
    FILE* f3 = fopen(p3, "w");
    if (f3) { bp_probe_dump_json(f3, &s_v3); fclose(f3); }
    printf("[probe_motor] Exported target-ABI layouts to '%s' and '%s'\n", p1, p3);
    return 0;
}
