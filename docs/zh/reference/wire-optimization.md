# Wire 布局与资源优化

## 范围与兼容

本轮精简帧长度规划、同步编码、ACK 位图借用及 RX 临时数据。wire v3、已有公开结构 ABI、同步 PHY 生命周期和外部串行化契约保持。私有 helper 与参数结构不作为 SDK 接口；产品继续通过公开 API 使用协议。

改动不增加每个 peer 或 PHY parser 的长期状态，不引入堆或重复基础组件实现。wire、datalink、network、transport、security 的职责仍按[架构](../protocol/architecture.md)划分。

## 长度计算和同步编码

`xgl_frame_measure_lengths` 统一校验 payload、TLV 扩展、认证标签及完整帧长度。size helper 对 wire 字段不能表示的长度返回零；payload budget 对失败清零输出，成功值不超过 16 位 payload 字段的范围。调用者不能仅以主机 `size_t` 加法没有溢出推断帧合法。

`xgl_frame_measure` 进一步检查借用字节 span 和认证扩展冲突。`xgl_frame_encode_into`、`xgl_frame_serialize` 和认证序列化的常规入口仍检查参数及目标容量。

datalink 在同一次同步发送中检查 layout、原地 payload 偏移和目标容量，然后通过私有 measured 入口继续编码。认证路径复用同一 layout，再签名及完成 CRC；普通路径直接编码和完成 CRC，最后经同一个 datalink PHY 提交出口发送。

| 私有入口的前提 | 要求 |
| --- | --- |
| layout 来源 | 由当前 frame 与标签长度成功测得 |
| 字节存储 | frame、payload/TLV spans 和 layout 在继续编码前保持不变 |
| 目标容量 | 已确认至少可容纳 `layout.frame_len` |
| 调用顺序 | 同次同步操作，不缓存 layout 用于下一帧 |
| 回调 | 不修改借用输入或重入实例 |

measured 入口是内部流程的继续执行点，不能接收任意网络输入或调用者自造 layout。它保留必要的 wire 字段编码检查，不替代认证关联、nonce 生命周期、busy 和广播策略。公开发送契约见[发送 API](../guide/send-api.md)。

## SACK 视图与接纳边界

`xgl_wire_decode_sack_view` 校验结构并借用原 TLV 位图，不复制字节；失败清零视图。原复制式 helper 使用同一解码规则，所有检查通过之前不修改调用者输出。

transport 不再预留 64 字节临时位图，但继续显式拒绝大于 64 字节、空位图或确认未来编号的反馈。整条反馈先校验，再提交 ACK 与重传操作；超长反馈不能先删除一部分可靠记录。wire 视图支持的编码长度和 transport 接受范围是两个不同契约。

位图只在当前同步处理期间借用，不保存到 peer 或队列。可靠传输的确认、容量与重试语义见[可靠性](../protocol/reliability.md)。

## 解码与 CRC 临时存储

固定头 CRC 使用 xgen-crc 的增量接口处理前 22 字节及两个零字节，替代 24 字节临时头副本。CRC 算法、种子和字节范围不变，基础算法继续由独立组件维护。

完整帧解码直接写入输出 view 的 header；失败出口清零整个 view，成功后才发布 payload/TLV spans。原始帧入口保留固定头、TLV 边界、认证标签长度、一致性和整帧 CRC 检查。流式 parser 的头校验、状态、超时和重同步规则不变。

没有为每个 link 缓存完整视图。曾试验通过私有入口跳过完成时的第二次头 CRC：同条件最终 ELF 比 CRC 精简版本增加 56 B，仍需在最终交付时解析 TLV。该试验没有进入生产代码。

## 私有结构与源码组织

`xgl_frame_params_t` 删除构造器没有消费的 `data_type` 和 `session_epoch` 字段；network 仍将它们编码进 DATA_TYPE 与 SESSION 扩展。零拷贝构造 helper 合入 `xgl_frame.c`，保持共享的长度和编码逻辑。

文件合并本身不代表更小固件。该零拷贝 helper 在起点 Boot ELF 中没有链接，删除独立文件的资源收益不能计为它的源码或对象大小。实际帧构造、长度规划和 CRC 函数仍由生产路径调用。

## 资源测量

比较必须固定工具链、profile、功能开关、MTU/RX、peer 和窗口容量，并记录最终 ELF、map、`.su`、源码状态与依赖版本。工作区已经包含在静态 RAM 中；额外栈预留不代表实际最大栈。

本轮整合实现已完成本地 Cortex-M0、GCC 15.2.1、`-Os`、单 peer、window 1 的最终 ELF 测量。每行都额外预留 1,024 B 栈：

| MTU/RX | LTO | Flash | workspace | 静态 RAM | RAM 含预留栈 | 最大单函数栈 |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 128 | 关闭 | 15,932 B | 1,136 B | 1,200 B | 2,224 B | 272 B |
| 64 | 关闭 | 15,932 B | 944 B | 1,008 B | 2,032 B | 272 B |
| 128 | 开启 | 12,600 B | 1,136 B | 1,200 B | 2,224 B | 312 B |
| 64 | 开启 | 12,600 B | 944 B | 1,008 B | 2,032 B | 312 B |

无 LTO 同配置相对起点的 16,060 B 减少 128 B，workspace 与静态 RAM 不变。本轮启用 LTO 比本轮关闭时减少 3,332 B，但最大单函数栈增加 40 B。此前源码的独立 LTO 试验为 12,472 B，本轮为 12,600 B，增加 128 B；不能用旧试验替代本轮结果。所有组合仍未达到 8 KiB Flash 目标。

本地完整矩阵保存在 `out/wire-opt-root/final/arm-{128,64}` 及对应 `-lto` 目录。报告记录源码状态、`lto`、实际优化标志和目标 IPO 状态。正式探针选项 `XGL_FOOTPRINT_LTO` 默认关闭；开启时对协议与选中的基础库及最终链接启用 LTO，消费者和布局诊断对象保持非 LTO，以保留通用消费边界和可检查的布局。LTO 组合使用独立预算文件，不放宽默认组合限额。

隔离 RX 实验固定其他链接对象，得到以下 Cortex-M0、GCC 15.2.1、`-Os`、无 LTO、Boot MTU/RX 128 数据。这些是局部候选的对比，不是本轮整合版资源：

| 方案 | 最终 Flash | 头 CRC 单函数栈 | 完帧解码单函数栈 |
| --- | ---: | ---: | ---: |
| 起点实现 | 16,060 B | 32 B | 136 B |
| 增量 CRC | 16,056 B | 16 B | 136 B |
| 增量 CRC 与直接 header 解码 | 16,044 B | 16 B | 96 B |

原始证据位于本地 `out/wire-opt-rx/`。各候选收益不能与其他源码修改机械相加。LTO 会改变跨文件内联和符号归属，不能只求剩余 wire 符号之和来报告 LTO 后占用。

以上是通用探针的本地链接结果，没有在目标板执行。真实产品仍需加入 BSP、PHY/DMA、升级业务、Flash 写入缓存及中断栈；见[资源模型](../guide/resource-model.md)。最大单函数栈不是整条调用链的栈上限。

## 验证与复现

主机先配置显式安装包，或使用 `dev/` 并提供五个独立基础组件路径。测试使用固定 GoogleTest 1.16.0，生产配置不下载依赖。以下以已经配置的构建目录为例：

```sh
cmake --build build/dev-debug --parallel 6
ctest --test-dir build/dev-debug --output-on-failure
python tools/quality.py test --build-dir build/dev-debug
python tools/quality.py format
```

新 SACK 视图和 checked 长度行为保留 RED/GREEN 证据；RX 四项已有契约先在旧实现通过，再以生产实现验证重构。局部 wire/frame/parser/datalink CTest 152 项通过。本地 GNU CTest 的 Full、Embedded 各 630 项，Boot 79 项，Embedded Lite 114 项通过；这些是各自配置下的 CTest 数量，不统一称为 GoogleTest 数量。其他编译器、sanitizer、覆盖率及最终质量门禁分别记录，不能从上述测试推断通过。

ARM 使用 `tools/boot_footprint` 的受控工程与工具链；从仓库根目录运行，路径替换为已准备的实际仓库：

```sh
cmake -S tools/boot_footprint -B build/wire-arm-128 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/xgen-link/tools/boot_footprint/toolchain-arm-cortex-m0.cmake \
  -DCMAKE_BUILD_TYPE=MinSizeRel \
  -DXGL_DEV_STATUS_SOURCE_DIR=/path/to/xgen-status \
  -DXGL_DEV_BYTES_SOURCE_DIR=/path/to/xgen-bytes \
  -DXGL_DEV_CRC_SOURCE_DIR=/path/to/xgen-crc \
  -DXGL_DEV_MEMORY_SOURCE_DIR=/path/to/xgen-memory \
  -DXGL_DEV_CONTAINERS_SOURCE_DIR=/path/to/xgen-containers \
  -DXGL_FOOTPRINT_FRAME_SIZE=128 -DXGL_FOOTPRINT_RX_SIZE=128
cmake --build build/wire-arm-128 --parallel 6
python tools/boot_footprint/check_budget.py \
  --report build/wire-arm-128/boot-footprint.json \
  --policy tools/boot_footprint/budget-mtu128.json \
  --output build/wire-arm-128/budget-result.json
```

64 字节配置使用独立目录，同时将 MTU/RX 改为 64 并选用 `budget-mtu64.json`。测量 LTO 时在独立的 `build/wire-arm-128-lto` 或 `build/wire-arm-64-lto` 重复相应配置，并增加 `-DXGL_FOOTPRINT_LTO=ON`；预算文件分别为 `budget-mtu128-lto.json`、`budget-mtu64-lto.json`，不能把非 LTO 报告交给 LTO policy。

完整流程见[发布验证](release-validation.md)。本地测试、远端 CI、源码发布和板级验收分别报告；本页没有声称新的远端或硬件结果。
