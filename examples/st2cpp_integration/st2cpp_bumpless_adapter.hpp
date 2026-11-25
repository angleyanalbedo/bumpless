/*
 * st2cpp_bumpless_adapter.hpp
 *
 * Lightweight C++11 adapter bridging ST2CPP/runtime-flat's cyclic scan model
 * (syncInputs -> LogicSolve -> syncOutputs) with Bumpless online hot-update.
 */
#ifndef ST2CPP_BUMPLESS_ADAPTER_HPP
#define ST2CPP_BUMPLESS_ADAPTER_HPP

#define BUMPLESS_HOST_IMPL
#include "bumpless.h"
#include "bp_handover.h"
#include <cstdint>

namespace st2cpp_demo {

/* Mirrors ST2CPP's TCI (Target Communication Interface) */
class MockTCI {
public:
    virtual ~MockTCI() = default;
    virtual void syncInputs(BpIoImage* io) = 0;
    virtual void syncOutputs(const BpIoImage* io) = 0;
};

/* Mirrors ST2CPP runtime-flat's Runtime::scanOnce() with Bumpless Quiescent Point */
class BumplessRuntimeAdapter {
public:
    explicit BumplessRuntimeAdapter(size_t cycle_copy_budget_bytes = 4096) {
        bp_host_init(&host_, cycle_copy_budget_bytes);
    }

    ~BumplessRuntimeAdapter() {
        bp_host_shutdown(&host_);
    }

    BpStatus loadInitialModule(const char* so_path) {
        return bp_host_stage_func(&host_, so_path);
    }

    BpStatus bindInstance(uint32_t inst_id, uint32_t fn_id) {
        return bp_host_bind_instance(&host_, inst_id, fn_id);
    }

    /* Non-RT background call (e.g. triggered via NetworkTCI / REPL command) */
    BpStatus stageOnlineChange(const char* so_path, const char* plan_path = nullptr) {
        return bp_host_stage_func_ex(&host_, so_path, plan_path);
    }

    void setTCI(MockTCI* tci) { tci_ = tci; }

    /* Equivalent to rt.scanOnce() in ST2CPP/runtime-flat */
    void scanOnce(uint32_t main_fn_id, uint32_t main_inst_id) {
        if (tci_) {
            tci_->syncInputs(&host_.io);
        }

        /* Quiescent commit + LogicSolve inside VEH/sigaction crash guard */
        bp_host_rt_cycle(&host_, main_fn_id, main_inst_id, bp_monotonic_ns());

        if (tci_) {
            tci_->syncOutputs(&host_.io);
        }
    }

    const BpPerfMetrics* metrics() const {
        return bp_host_get_metrics(&host_);
    }

private:
    BpHost   host_{};
    MockTCI* tci_ = nullptr;
};

} // namespace st2cpp_demo

#endif // ST2CPP_BUMPLESS_ADAPTER_HPP
