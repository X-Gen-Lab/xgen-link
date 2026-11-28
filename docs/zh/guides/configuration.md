---
id: guides/configuration
type: how-to
audience: user
source_language: zh_CN
---

# 配置

应用在自己的代码中初始化 `xgl_config_t`。下面的 C11 示例为单对端、单个在途包分配容量。64 字节是完整帧上限，ACK 超时为 200 毫秒，最多重试三次。

```c
xgl_config_t config = {
    .source_id = 1U,
    .memory = {.rx_buffer_size = 64U},
    .protocol = {.ack_timeout_ms = 200U, .max_retry_count = 3U,
                 .window_size = 1U, .max_frame_size = 64U},
    .features = {.max_tx_packets = 1U, .max_peers = 1U},
};
```

通信前填写路由、PHY 和接收回调。用 `xgl_config_validate(&config)` 检查参数，再用 `xgl_memory_requirements()` 查询目标上的工作区字节数和对齐要求。

## 生命周期和身份

实例借用不可变的 `xgl_config_t`，直到 `xgl_destroy()`。其路由数组、PHY 描述符、分配器服务、认证 provider、名称和回调上下文也必须持续有效。创建后不得修改配置，也不能让配置与 workspace 重叠。

`source_id` 必须非零且不同于 `XGL_BROADCAST_ID`。路由决定可达目标。接收应用数据需要接收处理函数；`rx_accept_callback` 优先于返回 void 的旧式 `rx_callback`。

## 路由和帧

| 字段 | 含义 |
| --- | --- |
| `target_id` | 目标节点 |
| `phy` | 借用的同步 TX、非阻塞 RX 回调 |
| `max_frame_size` | 此路由允许的完整序列化帧上限 |
| `read_freq_hz` | owner 调度时的读取频率提示，不创建定时器或任务 |
| `metric` | 保留的度量字段，当前不参与路径选择 |

路由容量在初始化时固定。共享同一 PHY 描述符的路由共享 parser/link；不同 PHY 描述符拥有独立 RX 状态。

实际轮询还受 step 调用频率、字节预算及回调时长约束。同一 PHY 使用共享路由中的最高频率提示；协议不能保证调度器未提供的吞吐量。

临时 PHY 读取块通过 CMake 的 `XGL_DATALINK_RX_CHUNK_SIZE` 选择：AUTO 默认 Boot 为 32，其余为 128，也支持显式 16/32/64/128，例如 `-DXGL_DATALINK_RX_CHUNK_SIZE=64`。它限制每次读取的栈缓冲，不改变 MTU 或持久 RX 空间。实际值写入生成的配置头；不得给已安装库的消费方注入冲突宏。吞吐与超时的配合见 [Datalink](../explanation/datalink.md)。

实际帧必须同时满足协议上限和路由 MTU，包括 24 字节基础头、扩展、payload、启用认证时的 tag 和 2 字节 CRC。`memory.rx_buffer_size` 至少为 `protocol.max_frame_size`。

生产 SDK 的 `XGL_DATALINK_MAX_FRAME_SIZE` 默认 2048 字节，可在构建库时通过同名 CMake 选项在 64–2048 字节之间选择。实例 MTU 必须处于 64 字节与所选上限之间。单条路由可以声明更大的物理 MTU，发送时两项限制同时生效。接收存储设为同一 MTU 可避免额外预留。涉及可靠通信时同时检查[反向反馈预算](../reference/protocol/v1/routing.md)。

## 显式容量

| 配置字段 | 上限 |
| --- | --- |
| `features.max_peers` | 同时存在的 connection/epoch scope 数 |
| `protocol.window_size` | 每个 peer 的可靠在途包，1–32，且不超过编译窗口容量 |
| `features.max_tx_packets` | 整个实例的可靠在途包 |
| `features.max_rx_buffered_packets` | 整个实例的乱序包 |

启用的容量必须显式给出；零不表示无限制。peer 和 TX 包容量必须非零。窗口大于一时，乱序容量必须非零。关闭的可选资源可以为零。全局 TX 预算可以小于 `max_peers * window_size`，此时更早出现背压。

workspace 为各类包预留固定大小的槽位。需要减少预留 RAM 时，重新核算完整帧上限和各资源的槽位数量。参见[资源模型](../explanation/resource-model.md)。

显式编译 `XGL_ENABLE_ADVANCED_RUNTIME=ON` 后，可通过独立版本化描述符限制可靠发送的忙期并接收带 scope 的回调，见[扩展运行选项](advanced-runtime.md)。该能力默认关闭。安装消费可要求 `advanced_runtime` 能力。

## 认证

启用认证能力的 profile 要求所有非空 `auth_provider` 都包含同步 sign/verify 回调以及 `1..XGL_AUTH_TAG_MAX_LEN` 的 tag 长度，即使 `auth_required` 为 false。`auth_required` 为 true 时还要求 provider 非空。无效 provider 在配置校验和工作区测量阶段被拒绝，创建不会先分配工作区。发送认证流量前，用 `xgl_install_security_session()` 安装可信的双向参数。

`auth_required` 为 true 时，`protocol.max_frame_size` 必须支持本机基本认证收发能力。校验复用 wire 编码与帧测量，检查一个普通 `ACK_RANGE` 的完整认证帧；它覆盖零 epoch 的基本 HELLO 和最小 DATA 所需空间。32 字节 tag 对应的当前基础门槛为 88 字节，64 字节协议上限会被前置拒绝。

此门槛不包含可选的 `SESSION` 和应用 `DATA_TYPE` 扩展，也不保证任意元数据组合都能发送。单条路由可以具有更小的 MTU，用于转发或其他能容纳的帧；本机发送和转发仍逐帧检查真实长度。例如，协议上限为 128、路由 MTU 为 64、tag 为 32 的配置可以初始化并转发短帧，但不能通过该路由发送本机认证 DATA。`auth_required` 控制本机认证收发，不为非本地转发帧增加端点认证。

认证不要求 libc 分配器，静态 workspace 同样支持。密钥、前缀、epoch 和重启后的新鲜性由应用负责。普通报文不能建立可信会话。关闭会话后保留 nonce 域的占用记录。

## 校验和编译 Profile

只支持以下明确的能力组合，不提供任意 feature flag 的排列。实际启用项及容量仍由运行时配置决定，运行时配置不能恢复编译时已裁剪的能力。

| Profile | 认证/转发 | 乱序 | 统计/诊断文字 |
| --- | --- | --- | --- |
| `boot` | 无 | 无 | 无 |
| `embedded-lite` | 无 | 有 | 有 |
| `embedded` | 有 | 有 | 有 |
| `full` | 有 | 有 | 有 |

Boot 允许一个 peer、最多一条路由、窗口一。Embedded Lite 默认使用 8 个可靠索引桶，不保留安全会话存储；适用于需要乱序缓存的直接通信端点。其他容量上限由生成的 `xgl_build_config.h` 给出。

可靠索引桶数、私有窗口容量和自适应 RTO 可以通过受控编译选项选择，默认私有窗口仍为 128 槽，自适应 RTO 默认开启。运行时配置不能扩大已编译容量。选项、安装消费和冲突拒绝见[构建与测试](build-and-consume.md)。

安装包导出 `xgl_PROFILE`、`xgl_RESOURCE_CONFIG_ID`、`xgl_CAPABILITIES` 和实际资源选项，目标 `xgl::xgl` 同时具有对应的 档位、资源标识和能力属性。消费者可用 `find_package(xgl CONFIG REQUIRED COMPONENTS reliable out_of_order)` 要求能力；不支持的必需能力在配置阶段拒绝。可请求名称为 `wire`、`reliable`、`authentication`、`forwarding`、`out_of_order`、`statistics`、`diagnostics` 和 `advanced_runtime`。

压缩和 payload 加密仍是保留项，启用会被拒绝；两个 feature flag 保持 false，`compression_id` 保持零。通过 `xgl_memory_requirements()` 检查 workspace 算术溢出并查询依赖 ABI 的真实大小。库、CMake target 与消费者必须使用匹配的生成 profile 和资源头文件。
