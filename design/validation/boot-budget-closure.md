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

## 等价优化与 GREEN

本轮生产修改限于四个现有文件，没有改变公开结构、wire 格式、资源选择或基础组件版本：

- `xgl_route.c`：整表验证成功后批量 `memmove`，直接重建可选索引，避免再次逐条查找和添加。重叠的自有子数组、零条空指针加载保持有效；索引失败仍显式返回错误，不留下部分可查找索引。
- `xgl_instance_links.c`：同一 PHY 先求最大非零频率，再做一次毫秒换算，保留最短 1 ms 和内部零频率处理。公开配置仍拒绝零频率。
- `xgl_transport_ack.c`：ACK_RANGE 在已排除零长度后比较长度减一；SACK 先验证基址，再比较可用差值，避免 64 位加法且不发生回绕。最高置位位置从末尾非零字节定位，最多扫描 64 个字节并移位 7 次，替代逐位扫描最多 512 次。仍在任何确认/重传标记前验证整个声明。
- `xgl_transport_runtime.c`：两类重传计时先选择起点和间隔，再统一归并 deadline；快速重传和背压期限的优先级不变。

新增九项行为契约在生产修改前通过，作为等价优化的基线，不把这些成功测试称为行为 RED。Full route/轮询 38/38、Boot route 30/30、SACK 8/8；证据在 `out/boot-route-contracts/` 与 `out/boot-transport-size/`。路由用例移至所有 profile 的常规集合，分配失败用例按实际索引能力验证分配次数。中间 `links-before.xml` 包含已纠正的零频率接受假设，最终基线使用 `full-baseline.xml`。

资源 RED `bf40b81` 之后，主工作区重新链接全部八个配置，均通过原 policy。下表 MTU128/64 两种 Flash 相同，RAM 分列；单函数栈普通/LTO 为 192/208 B，与本轮前一致。

| 编译配置 | Flash 普通 / LTO | 相对本轮前减少 | MTU128/64 工作区 | RAM 含 1 KiB 预留栈 |
| --- | ---: | ---: | ---: | ---: |
| 默认 window128、RTO ON | 16,100 / 12,780 B | 248 / 184 B | 1,072 / 880 B | 2,160 / 1,968 B |
| small window1、RTO OFF | 15,868 / 12,612 B | 248 / 176 B | 1,040 / 848 B | 2,128 / 1,936 B |

八组报告、ELF 和 map 保存在 `out/boot-budget/{default,small}-{128,64}-{off,on}/`，汇总为 `arm-summary.json`。普通 Flash 上限仍为 16,384 B，LTO 仍为 12,800 B；默认 LTO 余量为 20 B。依赖、consumer 探针、优化参数及预算均未调整。

最终逻辑提交为 `7c7887a`。Windows GNU Full/Embedded 各 817/817、Boot 222/222、Lite 314/314 通过；包括安装消费和能力支持的示例。对应日志及 JUnit 为 `out/boot-budget/{full,embedded,boot,embedded-lite}-{build.log,test.log,results.xml}`。

## 最终质量与来源

| 检查 | 实际结果 | 本轮证据（相对 `out/boot-budget/`） |
| --- | --- | --- |
| MSVC Full | 817/817，保留 510 个历史名称，无跳过/重复 | `msvc-summary.json`、`msvc-results.xml` |
| Full 固定 RTO 协议 | 814/814，无失败/跳过/重复 | `fixed-rto/summary.json` |
| Linux GNU 11.4 Full | 817/817，原有 808 项全部保留，新增九项 | `linux/evidence/full-test/` |
| Linux ASan/UBSan | 816/816，泄漏检查开启，原有 807 项全部保留 | `linux/evidence/asan-test/` |
| 生产覆盖率 | 行 4140/4414（93.792%）、函数 310/310（100%）、分支 2751/3272（84.077%） | `linux/evidence/full-coverage/` |
| 静态分析 | Cppcheck 2.21.0、Clang-Tidy 19.1.0 各 43 个生产翻译单元通过 | `quality-cppcheck.json`、`linux/evidence/tidy/` |
| 格式与文档 | 固定 formatter、文本、严格 Doxygen、双语 MkDocs 全部通过 | `format.log`、`text.log`、`docs.log`、`mkdocs.log` |

Sanitizer 配置仅不注册安装消费者 smoke，该消费者由普通 Full 实际执行；没有跳过协议测试。性质测试种子为 `5785420`。独立只读复审确认差值检查、倒序位图扫描、deadline 循环层级、路由重叠与 PHY 频率归约保持原有行为，未新增诊断抑制。

Linux 汇总为 `linux/verification-summary.json`，编译输入清单 SHA-256 为 `d37f7fd73c1a3fb35323bb11052d849001a62fcba3b223d00000efdc045a95bd`，构建前后及门禁后完全一致。执行期间将同一冻结工作区提交为 `7c7887a`，报告分别保留开始时的 `bf40b81` 加工作区修改和结束时的提交身份，不将 Git 提交变化当成源码变化，也不把本地验证标为远端 CI。

## 验证边界

本轮先处理已复现的资源回归。8 KiB Flash 目标、真实板卡调用链栈、PHY/DMA、密码 provider 和断电恢复仍需要各自的产品输入与验收，不能由主机回归或该链接探针推出。
