/*
 * bp_handover.h - Control-Theoretic Bumpless Handover Algorithm Library
 *
 * Ensures physical actuator outputs (voltage, current, torque, valve opening)
 * remain C0 continuous without mechanical shock (bump) during online hot-updates.
 *
 * Components:
 *   1. bp_pid_bumpless_track : Integral state back-calculation for PID gain changes
 *   2. bp_rate_limiter_step  : Slew-rate / acceleration clamp for step changes
 *   3. bp_ema_handover_step  : First-order exponential low-pass smoothing
 *   4. BpHandoverBlend       : Multi-cycle cosine S-curve blending across N cycles
 */

#ifndef BP_HANDOVER_H
#define BP_HANDOVER_H

#include <math.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BP_PI
  #define BP_PI 3.14159265358979323846
#endif

/*
 * 1. PID Integral Tracking Compensator (积分器跟踪反算)
 *
 * Guarantees u_new(t+) == u_old(t-) at the exact instant of hot-swap when
 * proportional gain (Kp) or integral gain (Ki) changes:
 *   u_old = Kp_new * error + Ki_new * I_new
 *   => I_new = (u_old - Kp_new * error) / Ki_new
 */
static inline double bp_pid_bumpless_track(double old_output,
                                           double new_kp,
                                           double new_ki,
                                           double error) {
    if (fabs(new_ki) < 1e-12) {
        return 0.0;
    }
    return (old_output - new_kp * error) / new_ki;
}

/*
 * 2. Dynamic Slew-Rate Limiter (斜坡速率限制器)
 *
 * Clamps the maximum output delta per cycle to prevent gear backlash,
 * over-current spikes, or hydraulic water-hammer.
 */
static inline double bp_rate_limiter_step(double prev_output,
                                          double target_output,
                                          double max_delta) {
    if (max_delta <= 0.0) {
        return target_output;
    }
    double diff = target_output - prev_output;
    if (diff > max_delta) {
        return prev_output + max_delta;
    }
    if (diff < -max_delta) {
        return prev_output - max_delta;
    }
    return target_output;
}

/*
 * 3. First-Order Exponential Moving Average Handover (一阶惯性低通平滑)
 *
 * Smoothly converges actuator output toward new algorithm output with
 * smoothing factor alpha in (0, 1].
 */
static inline double bp_ema_handover_step(double prev_output,
                                          double target_output,
                                          double alpha) {
    if (alpha >= 1.0) return target_output;
    if (alpha <= 0.0) return prev_output;
    return alpha * target_output + (1.0 - alpha) * prev_output;
}

/*
 * 4. Multi-Cycle Minimum-Jerk S-Curve Blender (多周期 S 型平滑过渡器)
 *
 * Blends from captured `old_output` to `new_output` over `total_cycles`
 * using a cosine S-curve: w(t) = 0.5 * (1 - cos(pi * k / N)),
 * ensuring both C0 position and C1 velocity continuity at handover boundaries.
 */
typedef struct {
    double   captured_old_output;
    uint32_t current_cycle;
    uint32_t total_cycles;
    bool     active;
} BpHandoverBlend;

static inline void bp_handover_blend_init(BpHandoverBlend* blend,
                                          double old_output,
                                          uint32_t transition_cycles) {
    if (!blend) return;
    blend->captured_old_output = old_output;
    blend->current_cycle       = 0;
    blend->total_cycles        = transition_cycles;
    blend->active              = (transition_cycles > 0);
}

static inline double bp_handover_blend_step(BpHandoverBlend* blend,
                                            double new_algo_output) {
    if (!blend || !blend->active) {
        return new_algo_output;
    }
    blend->current_cycle++;
    if (blend->current_cycle >= blend->total_cycles) {
        blend->active = false;
        return new_algo_output;
    }
    double ratio = (double)blend->current_cycle / (double)blend->total_cycles;
    double w     = 0.5 * (1.0 - cos(BP_PI * ratio));
    return (1.0 - w) * blend->captured_old_output + w * new_algo_output;
}

#ifdef __cplusplus
}
#endif

#endif /* BP_HANDOVER_H */
