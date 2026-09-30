# 独立基础组件迁移验证

本记录区分已执行的主机验证、ARM 交叉链接及尚未执行的硬件验证。
link 原有工作区改动保留；不把这些改动全部打包进本轮检查点提交。

## 来源与基线

- 实施分支：`feat/independent-memory-containers`。
- 原始工作区、暂存区及 HEAD 记录位于本地
  `build/independent-migration/working-baseline.zip`、`baseline-*.txt`
  和 `staged.patch` / `unstaged.patch`，属于未安装的开发产物。
- core 来源：`dc5eb1167ba21de384e7a760a8586b87502b73b2`。
- GCC 13.2：旧 core 11/11 CTest（含安装消费）通过。
- GCC 13.2：旧 link 8/8 CTest（包含实际 GoogleTest 集合、四个示例、
  静态工作区、无堆及安装消费）通过。
- GCC 15.2.1 Cortex-M0：原探针 Flash 16,212 B、静态 RAM 1,480 B、
  workspace 1,416 B、预留栈 1,024 B、RAM 合计 2,504 B。

## ACK 位存储的 TDD 证据

RED 检查点：`b169a10574b1d2a3929ef0d331a35d6ccd460c5c`。
先增加 `CompactWindowTest` 并在原实现执行四个用例，其中两个失败：

1. 128 个 ACK 状态申请 128 B，没有满足 16 B 位存储契约。
2. 初始化分配失败后，调用者原有 allocator 字段被修改。

非字节对齐窗口反复复用，以及旋转后重置的两个回归用例通过。
失败证据在本地 `build/independent-migration/window-red.xml` 和日志。

GREEN：生产实现消费 `xgct::bitset`，保留紧凑借用视图与一字节循环偏移；
128 个标志申请 16 B，成功前不发布新窗口状态。相同四个用例全部通过。
证据在 `window-green.xml` 和日志；完整新依赖构建的 8/8 CTest 也通过。

执行入口：

```text
cmake --build build/independent-full --parallel 6
ctest --test-dir build/independent-full --output-on-failure --no-tests=error
build/independent-full/test/xgl_tests.exe --gtest_filter=CompactWindowTest.*
```

本检查点提交记录可重放测试和实际结果，生产迁移保留在工作区供整体评审，
不声称该提交单独包含此前尚未提交的完整协议重构。

## 接口与资源边界

独立组件使用 `xgm::`、`xgct::`、`xgb::`、`xgcrc::`、`xgs::` targets。
公开 allocator 类型随新组件迁移；wire v3 不因此更改版本。
配置不下载依赖，主机测试显式提供 GoogleTest/GoogleMock 1.16.0。

containers 的 DMA 缓冲接口不改变 link PHY 的同步消费契约。
DMA 时序、缓存维护、可访问内存和中断交接需要具体板级验证。

目标探针使用通用 Cortex-M0 布局，不是完整可烧录 Bootloader；
静态 RAM、链接器预留栈和实际调用链峰值分别记录。
最终组件验收与资源结果在迁移收尾时追加，不从主机通过推断硬件通过。
