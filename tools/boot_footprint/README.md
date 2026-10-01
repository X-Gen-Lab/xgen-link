# Cortex-M0 Boot 资源测量

这个独立消费者将真实的静态初始化、可靠发送、接收轮询、重传计时、超时查询和销毁路径链接成 ELF。它使用通用 64 KiB Flash / 8 KiB RAM 布局，不提供板级时钟、中断、UART、Flash 驱动或看门狗初始化，不能直接当成可烧录的 Bootloader。

需要 CMake、Ninja、Python 3 和 `arm-none-eabi-gcc` 工具链均可在 PATH 中找到。先显式准备 status、bytes、CRC、memory、containers 五个独立源码仓库；此诊断工程通过 `dev/Dependencies.cmake` 的 `xgl_dev_provide_components()` 装配目标，再消费正式 link 模块。没有默认 `external` 子模块，也不搜索固定兄弟目录；不能用 Host 安装的静态库替代 ARM 目标库。准备边界见[构建与测试](../../docs/zh/getting-started/build-and-test.md)。从 xgen-link 根目录运行，并将示例路径替换为实际位置：

```powershell
cmake -S tools/boot_footprint -B build/boot-footprint -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE="$PWD/tools/boot_footprint/toolchain-arm-cortex-m0.cmake" `
  -DXGL_DEV_STATUS_SOURCE_DIR="C:/path/to/xgen-status" `
  -DXGL_DEV_BYTES_SOURCE_DIR="C:/path/to/xgen-bytes" `
  -DXGL_DEV_CRC_SOURCE_DIR="C:/path/to/xgen-crc" `
  -DXGL_DEV_MEMORY_SOURCE_DIR="C:/path/to/xgen-memory" `
  -DXGL_DEV_CONTAINERS_SOURCE_DIR="C:/path/to/xgen-containers" `
  -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build/boot-footprint --parallel 6
```

产物包括 `xgl_boot_probe.elf`、`xgl_boot_probe.map`、`boot-footprint.json`、`workspace-layout.json` 和各源文件的 `.su`。构建结束自动检查最终 ELF 中没有 malloc、free、calloc、realloc、sbrk 或相应 newlib 堆服务，也不允许未解析符号。

`XGL_FOOTPRINT_LTO` 默认为 `OFF`，保留普通 `-Os` 基线。显式设置 `-DXGL_FOOTPRINT_LTO=ON` 时，配置先检查 C 编译器的 IPO 支持，再为协议和基础库开启 LTO，并在最终链接时应用 `-flto`。`layout_probe.c` 和消费者 `consumer.c` 保持普通对象，使布局常量可被读取，也避免编译器根据测量桩的空接收回调消除真实接收路径。没有自动启用或下载其他工具链；生产产品独立决定自身优化配置。

LTO 应使用独立构建目录。`footprint-optimization.json` 和最终报告的 `optimization` 记录库、布局对象、消费者的实际 IPO 属性及编译/链接选项；最终报告同时记录布尔 `lto` 字段。普通编译参数继续记录在 `build_parameters`。LTO 可能改变内联和栈布局，须比较最终 ELF 和 `.su`，不能根据较小 Flash 推断更小调用栈。

配置固定为一个 peer、一个路由、一个 link、window 1、一个可靠 TX 记录，默认 MTU 128、RX 128，关闭 AUTH、转发、分片、乱序和 libc allocator。可通过 `XGL_FOOTPRINT_FRAME_SIZE`、`XGL_FOOTPRINT_RX_SIZE` 显式选择较小帧和 RX 空间；MTU 范围为 64–2048，RX 必须不小于 MTU 且不大于 4096。布局对象、消费者和报告使用相同参数。全部容量显式给出；不可变配置为 `static const`，在消费者整个生命周期有效并可放入 Flash。

`layout_probe.c` 包含私有 workspace 类型头，不再包含生产 `.c` 文件；该诊断对象不链接到 ELF。脚本读取目标编译得到的版本 3 布局常量，分别计算初始化区域以及 peer、window、TX record、TX payload、scratch 五个独立资源池，生成精确大小的 workspace 数组；不能用最大块大小乘总块数代替这些分区。消费者入口仍调用真实 `xgl_memory_requirements` 核对该大小。交叉构建未执行目标代码，因此这项运行时比较只有在将消费者适配至目标或模拟器后才算 ARM 执行验证。

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

2026-10-01 前一五包迁移阶段在全新 `build/independent-arm-final` 使用当时默认的五个实际子模块完成交叉构建，没有源码路径覆盖。Flash 为 16,764 字节、静态 RAM 1,488 字节，其中 workspace 1,424 字节；额外预留栈 1,024 字节，合计 RAM 2,512 字节，最大已链接单函数栈 392 字节。相对旧基线增加 552 字节 Flash 与 8 字节 RAM。该阶段 ELF 无堆服务或未解析符号；报告记录实际五包路径和干净固定提交，详见[实施记录](../../REFACTORING_STATUS.md)。这些历史链接结果不表示本轮 dev 入口已经重跑，也不表示更小 Boot 目标或上板已经通过。

本轮已通过五个显式 `XGL_DEV_*_SOURCE_DIR` 在 `build/ownership-arm` 重新构建：Flash 16,764 B、静态 RAM 1,488 B、workspace 1,424 B、预留栈 1,024 B、合计 RAM 2,512 B、最大单函数栈 392 B，均与前一阶段相同；ELF 无堆服务或未解析符号。新报告记录实际源码路径、提交与已有脏状态，不宣称为干净的发布组合，也不改写上述历史报告的来源。`dev/dependencies.json` 仅固定诊断与 CI 的测试输入，产品必须自行选择和记录其实际源码组合。

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

证据分别位于 `out/refactor-arm-baseline` 与 `out/refactor-arm-final`，包含完整 ELF、map、布局、`.su` 和 JSON 报告；没有堆服务或未解析符号。满足本探针的 64 KiB Flash / 8 KiB RAM 限额，仍未达到 8 KiB Flash / 1 KiB workspace 目标。测量源码、工作区状态与验证边界见[协议结构验证记录](../../design/validation/protocol-structure.md)。

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

`toolchain-lock.json` 固定 Arm GNU 15.2.rel1/GCC 15.2.1 的官方 Linux 归档与官方 SHA-256，并记录校验和来源 URL。CI 在独立准备步骤下载、验证校验和并加入 PATH；生产配置及本地探针不下载工具。Windows 本地验证必须自行准备同版本的 ARM 工具链。

`budget-mtu128.json` 和 `budget-mtu64.json` 分别固定 profile、MTU/RX、编译器、基线来源及资源限额。Flash 的 16,384 B 是本轮回归上限，其他限额固定本轮实际 workspace、静态 RAM、预留栈、总 RAM 和最大单函数栈；它不是已达成的 8 KiB 设计目标。运行门槛：

```sh
python tools/boot_footprint/check_budget.py \
  --report build/boot-footprint/boot-footprint.json \
  --policy tools/boot_footprint/budget-mtu128.json \
  --output build/boot-footprint/budget-result.json
```

测量 64 时使用独立构建目录，并同时设置 `-DXGL_FOOTPRINT_FRAME_SIZE=64 -DXGL_FOOTPRINT_RX_SIZE=64`，选择对应 policy。任一限额超出、工具链/profile/容量不匹配、缺少测量、堆或未解析符号都会失败。CI 分别保存 ELF、map、`.su`、布局和预算差异 JSON；新增任务的远端结果必须按本次提交验证。

LTO 测量分别使用 `budget-mtu128-lto.json`、`budget-mtu64-lto.json`。这些独立预算的 Flash 上限为 12,800 B、最大单函数静态栈上限为 320 B，RAM 限额与对应普通配置相同；普通预算限额保持不变。预算要求报告的 `lto` 模式与自身一致，不能用 LTO 报告替代普通基线。首次正式选项测量为 Flash 12,600 B、最大单函数栈 312 B，来源与实际脏状态保存在各 policy 的 `baseline_source`，不表示远端 CI 或板级验收通过。

新增选项的真实配置、编译和报告契约可在准备好 ARM 工具链后运行：

```sh
python test/cmake/test_footprint_lto.py --link-source . --work-dir out/lto-contracts \
  --status-source /path/to/xgen-status --bytes-source /path/to/xgen-bytes \
  --crc-source /path/to/xgen-crc --memory-source /path/to/xgen-memory \
  --containers-source /path/to/xgen-containers
```

测试分别构建默认配置和 LTO 配置，核查真实编译/链接命令、布局与消费者隔离、完整报告、禁堆约束及预算模式拒绝。命令日志保存在显式工作目录中。
