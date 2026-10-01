# ADR 0008：有界接收、单向端点与读取块配置

状态：已采纳。日期：2026-10-01。

## 背景

在 wire 布局优化之后，接收路径仍存在两个已复现的恢复缺陷：重叠 magic 没有刷新候选起点；明显无法装入 cache 的扩展帧等到缓存写满才拒绝。可选 view 的兼容分支还让正常 feed 路径承担一个临时完整 view 的栈空间。

metadata 包装仅在 wire 状态与两个布尔值之间转换；四个双向层接口实际上都只使用一个方向。Boot 与其他 profile 共用 128 字节临时 PHY 读取块，但运行期预算不能缩小这个编译期栈数组。

## 决策

1. 在基础头通过校验时检查最小完整帧容量，取得认证扩展后再检查精确 trailer 容量。用减法比较避免载荷长度加法溢出，不跳过最终规范 wire 校验。
2. 重叠 magic 替换候选时刷新 timestamp；后续正常进度不刷新。半帧超时仍为总期限，支持单调 32 位时间回绕。
3. 私有 `feed_byte_view` 要求非空 view；旧 `feed_byte` 包装自行提供临时 view。生产热路径不承担兼容包装的局部视图，兼容入口仍保留严格校验。
4. 删除只用于 CRC 计数的 index，由已验证长度与 cache_len 推导完成；header 长度使用 wire 原生的 8 位范围。保留 cache_size、真实进度与候选时间，不访问全局实例配置替代每 PHY 状态。
5. 接收文件直接使用 wire view/status，删除 metadata 包装。私有 `consume_span` 同步消费稳定输入，处理完整帧交付、错误后继续与首错误返回；poll 单独负责超时及一次 PHY 读取。span 内仍逐字节推进 parser，不承诺批量解析性能或 RX 零拷贝。
6. packet 与 frame 分别使用 TX/RX 单向端点，每个端点只保存 context 与对应回调。实例负责绑定；协议层不通过拥有者句柄访问实例私有布局。
7. `XGL_DATALINK_RX_CHUNK_SIZE` 由 CMake 与生成配置头管理，支持 AUTO、16、32、64、128。AUTO 的 Boot 默认 32，其余默认 128。安装消费者看到真实编译值，不允许用冲突宏覆盖已编译库。

## 所有权与调度

span 不与 parser/cache 重叠，输入在同步消费期间稳定。完整 view 借用 parser cache，回调返回后立即失效；BUSY 不延长借用。需要重试的数据由上层依自身契约保存，datalink 不新增待投递队列、线程、堆或 DMA 完成 token。

超时由调用 span 的运行时维护；poll 保留“超时后仍读新字节、返回首错误”的行为。每次到期 PHY 只读一次，实际读取量是运行期预算与编译 chunk 的较小值。缩小 chunk 后必须核对吞吐、突发缓冲、轮询频率和整帧期限。

帧格式、CRC、超时、PHY 错误与上层准入分别按原有层次报告。不会为了统一计数在多层重复上报同一次驱动失败，具体解释见统计文档。

## 兼容与验证

wire v3、公开结构 ABI、profile ID、五个独立基础组件依赖及同步回调契约保持。更紧的 Boot 默认读取块影响一次 step 的工作量，产品可显式选回 128；这项变化必须在发布说明和配置文档中可见。

恢复和新增输入接口保留真实 TDD RED/GREEN；纯端点、文件和布局重构保持历史测试名称。验证包括所有分片位置、CRC 边界、错误后下一帧、BUSY 后继续消费、超时和时钟回绕、C11/C++17 头文件、安装消费、四 profile、sanitizer 与生产覆盖率。

最终资源同时固定 MCU、编译器、profile、MTU/RX、chunk 与 LTO 状态。预算检查拒绝缺失或不一致的 chunk 身份，不能用较小临时数组构建冒充原配置报告。工作区下降与栈下降分别解释；单函数静态栈不是调用链或 ISR 高水位。

详细证据见[本轮验证记录](../validation/datalink-optimization.md)，使用说明见[中文](../../docs/zh/reference/datalink-optimization.md)和[英文](../../docs/en/reference/datalink-optimization.md)。
