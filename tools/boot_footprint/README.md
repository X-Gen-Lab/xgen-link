# Cortex-M0 Boot 资源测量

## 可选扩展 API 探针

默认 `XGL_FOOTPRINT_ADVANCED_API=OFF` 保留原有静态创建、发送、步进和超时查询调用路径。仅设置 `XGL_ENABLE_ADVANCED_RUNTIME=ON` 会改变编译能力及部分私有布局，但链接器仍可能删除未调用的扩展 API，不能将该 ELF 的尺寸当作扩展接口的完整占用。

需要测量扩展 API 时，在独立构建目录同时指定 `-DXGL_ENABLE_ADVANCED_RUNTIME=ON -DXGL_FOOTPRINT_ADVANCED_API=ON`，按下文提供工具链、MTU/RX 和 LTO 参数。该模式改用 `xgl_memory_report_with_options()`、`xgl_init_static_with_options()`、`xgl_step_bounded()` 和 `xgl_resources_get()`，保留可靠发送、超时查询和销毁。options 使用默认值，步进限制为一个候选及一次维护发送；它没有覆盖所有选项组合，也不包含 Boot 裁掉的认证、分片和乱序实现。

报告的 `build_parameters` 记录两个选项，顶层 `advanced_runtime` 和 `advanced_api_probe` 分别标明编译能力和实际探针调用路径。现有八份默认/small 预算明确要求两者均为 `false`，阈值和历史基线不变；扩展探针报告需由产品另定预算，不能套用普通探针通过结论。预算格式允许显式布尔字段，缺失、类型不符或组合不符都会失败。所有结果仍只是交叉编译的最终 ELF，没有完成板上运行、驱动、时序或调用链栈验证。

使用固定 ARM 工具链验证两种调用路径及最终符号：

```sh
python test/cmake/test_footprint_advanced.py --link-source . --work-dir out/advanced-probe-contracts
```

## 通用测量方法

这个独立消费者将真实的静态初始化、可靠发送、接收轮询、重传计时、超时查询和销毁路径链接成 ELF。它使用通用 64 KiB Flash / 8 KiB RAM 布局，不提供板级时钟、中断、UART、Flash 驱动或看门狗初始化，不能直接当成可烧录的 Bootloader。

准备 CMake、Ninja、Python 3 和固定的 Arm GNU 工具链，并添加到 PATH。探针使用同一个源码树交叉编译协议库与消费者。生产构建与 SDK 的配置见[构建与测试](../../docs/zh/guides/build-and-consume.md)。从仓库根目录运行。

```powershell
cmake -S tools/boot_footprint -B build/boot-footprint -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE="$PWD/tools/boot_footprint/toolchain-arm-cortex-m0.cmake" `
  -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build/boot-footprint --parallel 6
```

产物包括 `xgl_boot_probe.elf`、`xgl_boot_probe.map`、`boot-footprint.json`、`workspace-layout.json` 和各源文件的 `.su`。构建结束自动检查最终 ELF 中没有 malloc、free、calloc、realloc、sbrk 或相应 newlib 堆服务，也不允许未解析符号。

`XGL_FOOTPRINT_LTO` 默认为 `OFF`，保留普通 `-Os` 基线。显式设置 `-DXGL_FOOTPRINT_LTO=ON` 时，配置先检查 C 编译器的 IPO 支持，再为协议库开启 LTO，并在最终链接时应用 `-flto`。`layout_probe.c` 和消费者 `consumer.c` 保持普通对象，使布局常量可被读取，也避免编译器根据测量桩的空接收回调消除真实接收路径。没有自动启用或下载其他工具链；生产产品独立决定自身优化配置。

LTO 应使用独立构建目录。`footprint-optimization.json` 和最终报告的 `optimization` 记录库、布局对象、消费者的实际 IPO 属性及编译/链接选项；最终报告同时记录布尔 `lto` 字段。普通编译参数继续记录在 `build_parameters`。LTO 可能改变内联和栈布局，须比较最终 ELF 和 `.su`，不能根据较小 Flash 推断更小调用栈。

配置固定为一个 peer、一个路由、一个 link、window 1、一个可靠 TX 记录，默认 MTU 128、RX 128，关闭 AUTH、转发、分片、乱序和 libc allocator。可通过 `XGL_FOOTPRINT_FRAME_SIZE`、`XGL_FOOTPRINT_RX_SIZE` 显式选择较小帧和 RX 空间；MTU 范围为 64–2048，RX 必须不小于 MTU 且不大于 4096。布局对象、消费者和报告使用相同参数。全部容量显式给出；不可变配置为 `static const`，在消费者整个生命周期有效并可放入 Flash。

`layout_probe.c` 包含私有 workspace 类型头，不包含生产 `.c` 文件；该诊断对象不链接到 ELF。脚本读取目标编译得到的版本 5 布局常量，分别计算初始化区域以及 peer、TX record、TX payload、scratch 四个独立资源池，生成精确大小的 workspace 数组；不能用最大块大小乘总块数代替这些分区。窗口位图已内嵌于 peer：`ack_bytes` 保留逻辑位图需求，`window_pool_blocks=0` 表示无独立窗口池，报告的 window 分区为零；公共 `xgl_memory_report` 的 `window_bytes` 同样为零，实际位图成本已计入 `peer_bytes`。旧版本布局和非零窗口池声明会被拒绝。消费者入口仍调用真实 `xgl_memory_requirements` 核对大小；交叉构建不执行目标代码，只有将消费者适配至目标或模拟器并执行后，这项比较才算 ARM 运行验证。

2026-09-30 旧 core 组合的 GCC 15.2.1 Cortex-M0 `-Os`、无 LTO 历史测量如下；2026-10-01 迁移前重跑确认同一基线。它不表示新五包组合的最终资源。

| 项目 | 字节 |
| --- | ---: |
| Flash（代码、只读数据及 `.data` 初始值） | 16,212 |
| 静态 RAM | 1,480 |
| 其中 workspace（8 字节对齐） | 1,416 |
| 额外预留栈 | 1,024 |
| RAM 合计 | 2,504 |
| 最大已链接单函数栈 | 392 |

最终 ELF 没有堆服务或未解析符号。此配置满足测量镜像的 64 KiB Flash / 8 KiB RAM 限额，尚未达到 8 KiB Flash / 1 KiB workspace 设计目标。

2026-10-01 前一五包迁移阶段在全新 `build/independent-arm-final` 使用当时默认的五个实际子模块完成交叉构建，没有源码路径覆盖。Flash 为 16,764 字节、静态 RAM 1,488 字节，其中 workspace 1,424 字节；额外预留栈 1,024 字节，合计 RAM 2,512 字节，最大已链接单函数栈 392 字节。相对旧基线增加 552 字节 Flash 与 8 字节 RAM。该阶段 ELF 无堆服务或未解析符号；报告记录实际五包路径和干净固定提交，详见[实施记录](https://github.com/X-Gen-Lab/xgen-link/blob/f09efca25abe84c344ce8c41d5645006c7b61df9/REFACTORING_STATUS.md)。这些结果对应当时的构建入口和探针组合。

随后在 `build/ownership-arm` 以五包源码重跑，Flash 16,764 B、静态 RAM 1,488 B、workspace 1,424 B、预留栈 1,024 B、合计 RAM 2,512 B、最大单函数栈 392 B，均与前一阶段相同。ELF 无堆服务或未解析符号。

报告中的 Flash 包括消费者、通用启动入口和实际链接的 C 库依赖。RAM 同时报告静态区与额外预留的 1024 字节栈；预留值不是实测栈用量。`.su` 只报告仍链接函数的单函数栈，不能替代调用链累计、ISR 嵌套或板上高水位测量。8 KiB 协议 Flash / 1 KiB workspace 是单独报告的设计目标，不满足时不会伪称达标。

## 协议结构优化后的对比

2026-10-01 在同一 GCC 15.2.1、Cortex-M0、`-Os`、无 LTO 和五包输入下，重新构建 `b158d60` 基线与本轮优化后的完整 ELF。接收 CRC 分类修复也计入最终值，未仅选取中间较小结果。

| 项目 | 重跑基线 | 优化后 | 变化 |
| --- | ---: | ---: | ---: |
| Flash | 16,764 B | 15,692 B | -1,072 B |
| workspace | 1,424 B | 1,168 B | -256 B |
| 静态 RAM | 1,488 B | 1,232 B | -256 B |
| 额外预留栈 | 1,024 B | 1,024 B | 0 B |
| RAM 合计 | 2,512 B | 2,256 B | -256 B |
| 最大已链接单函数栈 | 392 B | 392 B | 0 B |

证据分别位于 `out/refactor-arm-baseline` 与 `out/refactor-arm-final`，包含完整 ELF、map、布局、`.su` 和 JSON 报告；没有堆服务或未解析符号。满足本探针的 64 KiB Flash / 8 KiB RAM 限额，仍未达到 8 KiB Flash / 1 KiB workspace 目标。测量源码、工作区状态与验证边界见[协议结构验证记录](https://github.com/X-Gen-Lab/xgen-link/blob/f09efca25abe84c344ce8c41d5645006c7b61df9/design/validation/protocol-structure.md)。

## 本轮有界调度与容量优化

本轮使用相同 GCC 15.2.1、Cortex-M0、`-Os`、无 LTO 配置。默认 MTU/RX 128 的真实对比保留新增边界检查和调度修复带来的代码量，而不将不同容量混作同配置优化：

| 项目 | 前一轮 128 基线 | 本轮 128 | 同配置变化 | 本轮首次 64 |
| --- | ---: | ---: | ---: | ---: |
| Flash | 15,692 B | 16,060 B | +368 B | 16,060 B |
| workspace | 1,168 B | 1,136 B | -32 B | 944 B |
| 静态 RAM | 1,232 B | 1,200 B | -32 B | 1,008 B |
| 额外预留栈 | 1,024 B | 1,024 B | 0 B | 1,024 B |
| RAM 合计 | 2,256 B | 2,224 B | -32 B | 2,032 B |
| 最大已链接单函数栈 | 392 B | 272 B | -120 B | 272 B |

证据为 `out/opt-arm-128` 与 `out/opt-arm-64`。两份报告保留测量时的实际 revision、dirty 状态和源码内容哈希；64 为该配置首次测量，其回归基线从本轮开始，不宣称存在旧 64 测量。两者无堆或未解析符号；64 的 workspace 达到 1 KiB 目标，128 尚未达到。两者均未达到 8 KiB Flash 目标，也未完成板级验收。

## 固定工具链与自动门槛

`toolchain-lock.json` 固定 Arm GNU 15.2.rel1/GCC 15.2.1 的官方 Linux 归档与官方 SHA-256，并记录校验和来源 URL。CI 缓存按 runner 系统、架构和锁文件摘要区分的下载归档，每次解包前重新验证 SHA-256。缓存归档校验失败时，CI 重新下载并验证。生产配置及本地探针不下载工具。Windows 本地验证必须自行准备同版本的 ARM 工具链。

`budget-mtu128.json` 和 `budget-mtu64.json` 分别固定 profile、MTU/RX、编译器、基线来源及资源限额。Flash 的 16,384 B 是本轮回归上限，其他限额固定本轮实际 workspace、静态 RAM、预留栈、总 RAM 和最大单函数栈；它不是已达成的 8 KiB 设计目标。运行门槛：

```sh
python tools/boot_footprint/check_budget.py \
  --report build/boot-footprint/boot-footprint.json \
  --policy tools/boot_footprint/budget-mtu128.json \
  --output build/boot-footprint/budget-result.json
```

测量 64 时使用独立构建目录，并同时设置 `-DXGL_FOOTPRINT_FRAME_SIZE=64 -DXGL_FOOTPRINT_RX_SIZE=64`，选择对应 policy。任一限额超出、工具链/profile/容量不匹配、缺少测量、堆或未解析符号都会失败。CI 分别保存 ELF、map、`.su`、布局和预算差异 JSON；新增任务的远端结果必须按本次提交验证。

LTO 测量分别使用 `budget-mtu128-lto.json`、`budget-mtu64-lto.json`。这些独立预算的 Flash 上限为 12,800 B，当前最大单函数静态栈上限为 224 B，RAM 限额与对应普通配置相同；普通预算限额保持不变。预算要求报告的 `lto` 模式与自身一致，不能用 LTO 报告替代普通基线。首次正式选项测量为 Flash 12,600 B、最大单函数栈 312 B（当时上限为 320 B，历史基线不代表当前限额），来源与实际脏状态保存在各 policy 的 `baseline_source`，不表示远端 CI 或板级验收通过。

新增选项的真实配置、编译和报告契约可在准备好 ARM 工具链后运行：

```sh
python test/cmake/test_footprint_lto.py --link-source . --work-dir out/lto-contracts
```

CI 在 Default MTU128 的普通和 LTO 预算任务中复用已有构建。若已生成 `compile_commands.json` 并成功链接探针，可以用 `--build-dir` 指定构建目录，以 `--expect-lto OFF` 或 `--expect-lto ON` 核验对应模式。

独立运行时，测试分别构建默认配置和 LTO 配置。两种运行方式均核查真实编译及链接命令、布局与消费者隔离、完整报告和禁堆约束。CI 矩阵另用各自的预算策略核验资源限额。命令日志保存在显式工作目录中。

## Datalink 接收优化阶段的配置

Boot 的 `XGL_DATALINK_RX_CHUNK_SIZE=AUTO` 解析为 32 字节；可显式选择 16/32/64/128。版本 5 布局从目标编译导出实际 `rx_chunk_bytes`，报告与预算据此拒绝缺失或冲突配置。该参数独立于 MTU/RX cache，消费方不能用冲突的预处理宏覆盖已编译值。

`fb53b99` 阶段 Cortex-M0 非 LTO/LTO Flash 为 15,988/12,600 B；MTU128/64 workspace 为 1,112/920 B，静态 RAM 为 1,176/984 B，含 1,024 B 预留栈的总 RAM 为 2,200/2,008 B。最大单函数栈为 192/224 B。四份 policy 已收紧 RAM 与单函数栈限额，Flash 上限仍为 16,384/12,800 B。历史基线保留 chunk128，不把读取工作量变化写成相同配置。

Windows 与固定官方 Linux 工具链的四组合及收紧预算均通过，无堆与未解析符号。资源、源码、测试与未完成的板级验收边界见[本轮验证记录](https://github.com/X-Gen-Lab/xgen-link/blob/f09efca25abe84c344ce8c41d5645006c7b61df9/design/validation/datalink-optimization.md)。

## CRC 缓存重新同步

后续恢复实现复用同一 cache，并在交付后继续排空完整帧。更新后的非 LTO/LTO Flash 为 16,172/12,796 B，常驻 RAM 不变，最大单函数栈为 192/216 B。原预算保持，LTO 距 12,800 B 上限仅余 4 B；这些数字只适用于固定编译器与探针组合，不代表其他工具链具有同样余量。

首版 LTO 12,912 B 的超额报告与结构精简后的验证分别保留，见[缓存重新同步验证](https://github.com/X-Gen-Lab/xgen-link/blob/f09efca25abe84c344ce8c41d5645006c7b61df9/design/validation/parser-resynchronization.md)。错误恢复的 CRC 工作可能随候选数增长；资源预算通过不代替产品实时性或板级栈验收。

资源布局契约可独立运行 `python test/cmake/test_footprint_storage.py`，核对内嵌窗口不会产生额外池块、旧格式拒绝，以及预算对缺失或冲突 `window_pool_blocks` 的拒绝。该契约不替代最终 ELF 测量与完整调用链栈检查。

Transport 优化后的布局格式为 5。MTU128/64 工作区为 1,064/872 B，静态 RAM 为 1,128/936 B，另加 1,024 B 栈预留后为 2,152/1,960 B。普通/LTO Flash 为 16,116/12,792 B，既有 16,384/12,800 B 回归限额保持；LTO 仅余 8 B。普通 ACK 的已知调用链从 `xgl_step()` 起为 988/844 B，尚不含调用者、实际驱动和 ISR，预留栈不能当作板级峰值证明。具体冻结版本、Windows/Linux 对照和路径证据见[传输优化验证](https://github.com/X-Gen-Lab/xgen-link/blob/f09efca25abe84c344ce8c41d5645006c7b61df9/design/validation/transport-optimization.md)。

## 显式 transport 资源组合

格式 6 记录目标实编的 `resource_config_id`、`compiled_window_capacity`、`reliable_bucket_count` 和 `adaptive_rto`。默认 Boot 使用 128 槽、1 桶、自适应 RTO；实例的运行时窗口仍为 1，不能把它误当成编译容量。默认预算绑定资源身份 1857，原资源限额与历史基线不变。

small 独立构建明确追加 `-DXGL_TRANSPORT_WINDOW_CAPACITY=1 -DXGL_RELIABLE_INDEX_BUCKETS=1 -DXGL_ENABLE_ADAPTIVE_RTO=OFF`，资源身份为 1025，使用 `budget-small-mtu128.json`、`budget-small-mtu64.json` 及各自 `-lto` 文件。不要在同一目录切换组合，也不能使用 small 报告满足默认预算。

2026-10-02 最终等价优化后，默认非 LTO/LTO Flash 为 **16,100/12,780 B**，small 为 **15,868/12,612 B**，八组原预算全部通过；默认 LTO 余量为 20 B。默认 MTU128/64 workspace 为 1,072/880 B，small 为 1,040/848 B，RAM 和单函数栈均未增加。完整 RED/GREEN 与同配置对比见[预算收敛验证](https://github.com/X-Gen-Lab/xgen-link/blob/f09efca25abe84c344ce8c41d5645006c7b61df9/design/validation/boot-budget-closure.md)；前期超限及栈路径保留在[资源优化验证](https://github.com/X-Gen-Lab/xgen-link/blob/f09efca25abe84c344ce8c41d5645006c7b61df9/design/validation/transport-resource-refinement.md)。不同能力组合的差值不能称为同功能优化；1 KiB 栈预留和主机通过仍不代替板级验收。

## 可选有界运行与批量清理优化

`2e14f47` 冻结后默认普通/LTO 为 **16,060/12,752 B**，small 为 **15,828/12,580 B**；八组原预算通过，workspace 与栈指标保持。默认 LTO 余量为 48 B。

MTU128/LTO 的扩展宏 ON、旧入口探针为 **13,988 B Flash / 1,128 B workspace / 2,216 B RAM 含预留栈**；实际扩展 API 探针为 **14,424 / 1,128 / 2,224 B**。这两个模式分别记录，不用默认预算评价它们，也不代表所有扩展选项组合。完整源码、十份 ELF 和验证边界见[本轮记录](https://github.com/X-Gen-Lab/xgen-link/blob/f09efca25abe84c344ce8c41d5645006c7b61df9/design/validation/system-refinement.md)。

## 运行边界与资源证据完善

`bb02a75` 后八组默认 ELF 的已分配文件段与上一轮逐字节一致，原预算全部通过。扩展 Boot MTU128/LTO 的旧入口/真实新 API 探针为 **14,052/14,496 B Flash**，分别增加 64/72 B；workspace **1,128 B**、含 1 KiB 预留栈 RAM **2,216/2,224 B** 及最大单函数栈 **216 B** 均保持。详见[运行边界验证](https://github.com/X-Gen-Lab/xgen-link/blob/f09efca25abe84c344ce8c41d5645006c7b61df9/design/validation/runtime-boundaries.md)。

报告生成步骤拒绝缺失、损坏或无有限上界的已链接函数栈记录；错误写入 `stack_evidence_errors`，同时返回失败并将最大栈值设为 `null`，防止部分证据通过后续预算检查。源码摘要覆盖协议实现、公共头、CMake 配置及探针源码；摘要范围变化不等于固件体积变化。该检查仍不覆盖无 `.su` 记录的汇编/运行库函数，也不替代调用链和板级栈测量。
