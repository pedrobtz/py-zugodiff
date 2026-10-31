#pragma once
#include <stdint.h>
typedef struct gs_stack gs_stack;
#ifndef GS_ALIGN1
#if (defined(__GNUC__) || defined(__clang__)) && !defined(__TINYC__)
#define GS_ALIGN1 __attribute__((aligned(1)))
#else
#define GS_ALIGN1
#endif
typedef int16_t gs_i16 GS_ALIGN1;
typedef int32_t gs_i32 GS_ALIGN1;
typedef int64_t gs_i64 GS_ALIGN1;
typedef uint16_t gs_u16 GS_ALIGN1;
typedef uint32_t gs_u32 GS_ALIGN1;
typedef uint64_t gs_u64 GS_ALIGN1;
typedef float gs_f32 GS_ALIGN1;
typedef double gs_f64 GS_ALIGN1;
#endif
#pragma pack(push, 1)
typedef struct { uint8_t *base; int64_t len; } gs_rhdr;
typedef struct { gs_rhdr *hdr; gs_stack *stk; } gs_rref;
typedef struct { gs_rhdr *hdr; gs_stack *stk; gs_rhdr *fl; gs_stack *flstk; } gs_pref;

typedef struct { uint8_t *data; int64_t len; } sl_u8;

typedef struct { gs_i32 *data; int64_t len; } sl_i32;

typedef struct Part_g Part_g;
struct Part_g {
    int64_t xmid_g;
    int64_t ymid_g;
    uint8_t lo_minimal_g;
    uint8_t hi_minimal_g;
};

typedef struct Run_g Run_g;
typedef Run_g *r_Run_g GS_ALIGN1;
struct Run_g {
    int32_t op_g;
    int32_t o_g;
    int32_t n_g;
    int32_t len_g;
};

typedef struct Step_g Step_g;
struct Step_g {
    int32_t op_g;
    int32_t len_g;
};

typedef struct Task_g Task_g;
struct Task_g {
    int32_t kind_g;
    int32_t xoff_g;
    int32_t xlim_g;
    int32_t yoff_g;
    int32_t ylim_g;
    uint8_t find_minimal_g;
};

typedef struct { int64_t e[3]; } a3_i64;

#pragma pack(pop)

#ifdef __cplusplus
extern "C" {
#endif
void goose_init(int argc, char **argv);
const uint8_t * zudiff_diff_ids(sl_i32 gs_export_arg0, sl_i32 gs_export_arg1, gs_i32 gs_export_arg2, gs_i64 gs_export_arg3, gs_i64 *gs_export_len);
const uint8_t * zudiff_diff_bytes(sl_u8 gs_export_arg0_2, sl_u8 gs_export_arg1_2, gs_i32 gs_export_arg2_2, gs_i64 gs_export_arg3_2, gs_i64 *gs_export_len_2);
gs_i64 zudiff_probe_index(gs_i64 gs_export_arg0_3);
gs_i64 zudiff_probe_sum(sl_i32 gs_export_arg0_4);
gs_i64 zudiff_probe_stack(gs_i64 gs_export_arg0_5, uint8_t gs_export_arg1_3);
#ifdef __cplusplus
}
#endif
