# ST2CPP (`runtime-flat`) + Bumpless Integration Example

本示例演示如何将 **Bumpless** 无扰热更新引擎无缝嵌入到 [`ST2CPP/runtime-flat`](https://github.com/angleyanalbedo/ST2CPP/tree/master/runtime-flat) 的周期扫描调度器（`ReadInputs -> LogicSolve -> WriteOutputs`）中。

## 为什么两者是天生绝配？

1. **周期静默边界天然对齐**：
   `ST2CPP/runtime-flat` 的 `rt.scanOnce()` 严格遵循 `TCI::syncInputs() -> LogicSolve -> TCI::syncOutputs()`。在两次 `scanOnce()` 之间的周期间隙，所有 ST 编译生成的 C++ POU 调用栈深度归零，是 Bumpless 切换转发表与执行纯 `memcpy` 的理想静默点。
2. **裸金属 Target 极度契合“裸 POD 零膨胀”**：
   `ST2CPP/target/` 包含资源极度受限的平台（如 `stm32f1` 仅 **20 KB RAM / 64 KB Flash**、`stm32h7`、`bananapif3` RISC-V、`rk3588` PREEMPT_RT）。Bumpless 不在运行时动态库或固件中塞入任何字段名字符串和反射字典，离线生成的 `CopyPlan` 仅几十字节，完美契合裸机与嵌入式 Linux Target。
3. **TCI ProcessImage 与实例槽位双解耦**：
   `NetworkTCI` / `EtherCAT` 等硬件映射继续通过 `BpIoImage`（ProcessImage）在非实时线程与周期边界同步，而 POU 内部状态通过 `g_inst_table[inst_id]` 独立迁移，互不干扰。

## 编译与运行

```bash
make st2cpp-example
```
