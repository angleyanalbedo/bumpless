/*
 * tools/bp_probe.h - Target-ABI Isomorphic Struct Layout Probe
 *
 * This header is included by offline probe runners compiled with the EXACT
 * TARGET COMPILER and TARGET FLAGS (matching word size, endianness, struct alignment).
 *
 * It extracts:
 *   - Accurate member offsets (via offsetof)
 *   - Member sizes (via sizeof)
 *   - Automatic classification of vptr and pointer members (to be skipped)
 */

#ifndef BP_PROBE_H
#define BP_PROBE_H

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "bumpless.h"

typedef struct {
    const char* field_name;
    uint32_t    offset;
    uint32_t    size;
    uint16_t    type_id;
    bool        is_vptr;
    bool        is_pointer;
} BpProbeField;

typedef struct {
    const char*         struct_name;
    uint32_t            inst_size;
    uint64_t            layout_hash;
    size_t              field_count;
    const BpProbeField* fields;
} BpProbeStruct;

/* 64-bit FNV-1a hash */
static inline uint64_t bp_fnv1a_64(const void* data, size_t len, uint64_t hash) {
    const uint8_t* ptr = (const uint8_t*)data;
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint64_t)ptr[i];
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

static inline uint64_t bp_compute_layout_hash(const BpProbeField* fields, size_t count) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < count; i++) {
        h = bp_fnv1a_64(fields[i].field_name, strlen(fields[i].field_name), h);
        h = bp_fnv1a_64(&fields[i].offset, sizeof(fields[i].offset), h);
        h = bp_fnv1a_64(&fields[i].size, sizeof(fields[i].size), h);
        h = bp_fnv1a_64(&fields[i].type_id, sizeof(fields[i].type_id), h);
    }
    return h;
}

/* Emit JSON for a probed struct */
static inline void bp_probe_dump_json(FILE* out, const BpProbeStruct* s) {
    fprintf(out, "{\n");
    fprintf(out, "  \"struct\": \"%s\",\n", s->struct_name);
    fprintf(out, "  \"inst_size\": %u,\n", s->inst_size);
    fprintf(out, "  \"layout_hash\": \"0x%016llX\",\n", (unsigned long long)s->layout_hash);
    fprintf(out, "  \"fields\": [\n");
    for (size_t i = 0; i < s->field_count; i++) {
        const BpProbeField* f = &s->fields[i];
        fprintf(out, "    { \"name\": \"%s\", \"offset\": %u, \"size\": %u, \"type_id\": %u, \"is_vptr\": %s, \"is_pointer\": %s }%s\n",
                f->field_name, f->offset, f->size, f->type_id,
                f->is_vptr ? "true" : "false",
                f->is_pointer ? "true" : "false",
                (i + 1 < s->field_count) ? "," : "");
    }
    fprintf(out, "  ]\n");
    fprintf(out, "}\n");
}

#endif /* BP_PROBE_H */
