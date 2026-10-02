# 默认 Boot 资源预算收敛

## 目标与基线

在 `a1849d5` 基础上继续优化默认 Boot 的最终 ELF，保持 wire v3、公开 ABI、默认编译能力、输入校验及原预算。目标为默认 LTO Flash 不超过既有 12,800 B；同时复验普通模式与 small 组合，不能将关闭自适应 RTO 或减小编译窗口冒充同功能优化。

本轮仍在 `feat/independent-memory-containers`，保留既有 `.spec-workflow/`，只做本地修改与提交。

## 资源 RED

当前提交重新配置并编译成功后，实际执行原 `tools/boot_footprint/budget-mtu128-lto.json` 失败，退出 1。没有修改预算或探针消费逻辑。

| 输入/观察 | 实际值 |
| --- | --- |
| 工具链与目标 | Arm GNU 15.2.1，Cortex-M0，MinSizeRel，LTO |
| 编译配置 | Boot，window 128，bucket 1，自适应 RTO ON，RX chunk 32 |
| MTU / RX | 128 / 128 B |
| Flash / 原门槛 | 12,964 / 12,800 B，超限 164 B |
| 工作区 / 静态 RAM | 1,072 / 1,136 B |
| RAM 含预留栈 / 最大单函数栈 | 2,160 / 208 B，预留栈 1,024 B |
| 源码身份 | `a1849d59a5537752cdeb914abcd1d9c9398d222e`，生产输入 SHA-256 `c5e5e62115c91880349a9f1e2cd7a646727e1d79f89f5cd1ec5c20f5d52d0df4` |

证据保存在 `out/boot-budget/red-{configure,build}.log`、`red-footprint.json`、`red-budget-result.json` 及 `red.elf`。报告 dirty 标记包含原有未跟踪目录，不表示已验收干净克隆或远端 CI。

## 验证边界

本轮先处理已复现的资源回归。8 KiB Flash 目标、真实板卡调用链栈、PHY/DMA、密码 provider 和断电恢复仍需要各自的产品输入与验收，不能由主机回归或该链接探针推出。
