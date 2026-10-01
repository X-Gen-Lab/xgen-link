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

配置固定为一个 peer、一个路由、一个 link、window 1、一个可靠 TX 记录、MTU 128、RX 128，关闭 AUTH、转发、分片、乱序和 libc allocator。全部容量显式给出；不可变配置为 `static const`，在消费者整个生命周期有效并可放入 Flash。

`layout_probe.c` 仅为测量私有 workspace 类型而包含生产实现源码；该诊断对象不链接到 ELF。脚本读取目标编译得到的版本 3 布局常量，分别计算初始化区域以及 peer、window、TX record、TX payload、scratch 五个独立资源池，生成精确大小的 workspace 数组；不能用最大块大小乘总块数代替这些分区。消费者入口仍调用真实 `xgl_memory_requirements` 核对该大小。交叉构建未执行目标代码，因此这项运行时比较只有在将消费者适配至目标或模拟器后才算 ARM 执行验证。

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
