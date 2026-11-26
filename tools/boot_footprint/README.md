# Cortex-M0 Boot 资源测量

此工程链接真实的 XGL 静态初始化、可靠发送、接收轮询、重传计时、超时查询和销毁路径。通用探针采用 64 KiB Flash、8 KiB RAM 布局。主机 `xgl_footprint` 目标提供静态归档文件字节数，Cortex-M0 探针提供目标 ELF、布局和栈证据。 [构建与消费](../../docs/zh/guides/build-and-consume.md)

## 构建测量

准备 CMake、Ninja、Python 3 和 `arm-none-eabi` 工具链。CI 使用 `toolchain-lock.json` 固定的 Arm GNU 15.2.rel1/GCC 15.2.1 官方 Linux 归档，解包前核验 SHA‑256。本地将已安装的工具链加入 `PATH`。从仓库根目录运行。

```sh
cmake -S tools/boot_footprint -B build/boot-footprint -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/tools/boot_footprint/toolchain-arm-cortex-m0.cmake" \
  -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/boot-footprint --target xgl_boot_probe --parallel 4
```

默认构建同样生成 `xgl_boot_probe.elf`、`xgl_boot_probe.map`、`boot-footprint.json`、`boot-footprint.txt`、`workspace-layout.json`、`footprint-optimization.json` 和编译器的 `.su` 文件。重新构建目标可以从有效 ELF 重新生成缺失的报告。报告检查最终 ELF 的堆服务、未解析符号与已链接函数的有限栈证据，并记录编译器、配置、源码提交、工作区状态及源码摘要。摘要包括生产实现、公开头、CMake 配置和探针输入。

固定探针使用 Boot 档、单 peer、单路由、单 link、运行时 window 1 和单个可靠 TX 记录，关闭 libc allocator。默认 MTU/RX 均为 128 B。`XGL_FOOTPRINT_FRAME_SIZE` 可选 64–2048 B，`XGL_FOOTPRINT_RX_SIZE` 范围为 MTU–4096 B。Boot 的 `XGL_DATALINK_RX_CHUNK_SIZE=AUTO` 解析为 32 B，也可显式选择 16/32/64/128 B。布局对象与探针使用同一组编译配置。

`layout_probe.c` 以目标 ABI 编译实际私有类型并导出 **格式 6**。报告计算初始化区以及 peer、TX record、TX payload 和 scratch 各分区。`ack_bytes` 记录逻辑位图需求，位图占用计入 peer，`window_pool_blocks` 固定为 0。布局还记录已编译窗口容量、索引桶数量、RTO 选项与资源身份。消费者入口在执行时调用真实的内存需求 API 核对大小，交叉构建阶段读取目标布局对象。

## 资源组合与预算

八份预算覆盖两种编译资源组合、MTU/RX 64 或 128 B、普通 `-Os` 或 LTO。每种组合使用独立构建目录。

| 资源组合 | 编译设置 | 预算文件 |
| --- | --- | --- |
| 默认 | 128 槽、1 桶、自适应 RTO，资源身份 1857 | `budget-mtu{64,128}{,-lto}.json` |
| Small | `-DXGL_TRANSPORT_WINDOW_CAPACITY=1 -DXGL_RELIABLE_INDEX_BUCKETS=1 -DXGL_ENABLE_ADAPTIVE_RTO=OFF`，资源身份 1025 | `budget-small-mtu{64,128}{,-lto}.json` |

测量 MTU64 时指定 `-DXGL_FOOTPRINT_FRAME_SIZE=64 -DXGL_FOOTPRINT_RX_SIZE=64`。设置 `-DXGL_FOOTPRINT_LTO=ON` 后，CMake 先验证 C IPO，再对协议库编译和 ELF 最终链接启用 LTO。布局对象和外部消费者保持普通编译，使布局常量可读，并保留经过外部 PHY 回调的实际接收路径。`optimization` 记录实际 IPO scope 与选项。

```sh
python tools/boot_footprint/check_budget.py \
  --report build/boot-footprint/boot-footprint.json \
  --policy tools/boot_footprint/budget-mtu128.json \
  --output build/boot-footprint/budget-result.json
```

预算核对编译器、profile、帧与 RX 容量、LTO 和资源身份，并给出限额与历史基线差值。八份 policy 各自保存基线提交、源码摘要与限额。报告另行列出 8 KiB Flash 与 1 KiB workspace 设计目标。 [CI 矩阵](../../.github/workflows/ci.yml)

## 扩展 API 与验证入口

扩展运行时的两项证据均使用 `-DXGL_ENABLE_ADVANCED_RUNTIME=ON`。`XGL_FOOTPRINT_ADVANCED_API=OFF` 链接传统入口，`ON` 实际调用 `xgl_memory_report_with_options()`、`xgl_init_static_with_options()`、`xgl_step_bounded()` 和 `xgl_resources_get()`。后者使用默认 options，单次步进限定一个候选与一次维护发送。报告分别记录编译能力 `advanced_runtime` 和调用路径 `advanced_api_probe`。八份普通预算要求两项均为 `false`，扩展探针采用各自的测量证据。

```sh
python test/contracts/footprint/test_footprint_budget.py
python test/contracts/footprint/test_footprint_storage.py
python test/contracts/footprint/test_footprint_report.py
python test/contracts/footprint/test_footprint_lto.py --link-source . --work-dir out/lto-contracts
python test/contracts/footprint/test_footprint_advanced.py --link-source . --work-dir out/advanced-probe-contracts
```

前三个脚本分别核对预算、布局和报告约束。后两个需要 ARM 工具链，检验真实编译和链接命令及扩展 API 的最终符号。LTO 验证也可用 `--build-dir <dir> --expect-lto OFF|ON` 检查已有的 Ninja 构建，要求该构建导出 `compile_commands.json`。

Flash 包括消费者、启动入口和实际链接的 C 库占用。报告区分静态 RAM、workspace 和链接脚本预留栈。`.su` 提供已链接函数的单函数栈证据。板级验收另行测量调用链、ISR 与驱动叠加、栈高水位和目标时序。 [验证范围](../../docs/zh/development/validation.md)

## 历史测量

早期五包输入、布局格式 5 和资源收敛的表格留在固定修订版。复现时使用对应提交和配置。 [历史测量表](https://github.com/X-Gen-Lab/xgen-link/blob/eca6c06389fcc100dbf0ef8296ad57e0fa172d46/tools/boot_footprint/README.md) · [八组预算收敛验证](https://github.com/X-Gen-Lab/xgen-link/blob/f09efca25abe84c344ce8c41d5645006c7b61df9/design/validation/boot-budget-closure.md) · [运行边界与扩展探针记录](https://github.com/X-Gen-Lab/xgen-link/blob/f09efca25abe84c344ce8c41d5645006c7b61df9/design/validation/runtime-boundaries.md)
