# 配置

先选择与编译 profile 匹配的预设，补充应用参数，再调用 `xgl_config_validate()` 和 `xgl_memory_requirements()`，最后准备存储。预设只是初始容量组合，不代表已测量的 MCU RAM 占用。

## 生命周期和身份

实例借用不可变的 `xgl_config_t`，直到 `xgl_destroy()`。其路由数组、PHY 描述符、分配器服务、认证 provider、名称和回调上下文也必须持续有效。创建后不得修改配置，也不能让配置与 workspace 重叠。

`source_id` 必须非零且不同于 `XGL_BROADCAST_ID`。路由决定可达目标。接收应用数据需要接收处理函数；`rx_accept_callback` 优先于返回 void 的旧式 `rx_callback`。

## 路由和帧

| 字段 | 含义 |
| --- | --- |
| `target_id` | 目标节点 |
| `phy` | 借用的同步 TX、非阻塞 RX 回调 |
| `max_frame_size` | 此路由允许的完整序列化帧上限 |
| `read_freq_hz` | 链路轮询频率 |
| `metric` | 路由选择度量 |

路由容量在初始化时固定。共享同一 PHY 描述符的路由共享 parser/link；不同 PHY 描述符拥有独立 RX 状态。

实际帧必须同时满足协议上限和路由 MTU，包括 24 字节基础头、扩展、payload、启用认证时的 tag 和 2 字节 CRC。`memory.rx_buffer_size` 至少为 `protocol.max_frame_size`。

## 显式容量

| 配置字段 | 上限 |
| --- | --- |
| `features.max_peers` | 同时存在的 connection/epoch scope 数 |
| `protocol.window_size` | 每个 peer 的可靠在途包，1–32 |
| `features.max_tx_packets` | 整个实例的可靠在途包 |
| `features.max_rx_buffered_packets` | 整个实例的乱序包 |
| `features.max_message_size` | 单条分片收发消息的最大长度 |
| `features.max_reassembly_slots` | 同时未完成的重组数 |
| `features.max_reassembly_bytes` | 未完成重组与已完成待交付消息共享的 RX 字节预算 |
| `features.max_tx_message_bytes` | 等待发送推进的消息副本共享字节预算 |

启用的容量必须显式给出；零不表示无限制。peer 和 TX 包容量必须非零。窗口大于一时，乱序容量必须非零。启用分片时，消息长度、重组槽数和两个消息字节预算必须非零。关闭的可选资源可以为零。全局 TX 预算可以小于 `max_peers * window_size`，此时更早出现背压。

字节预算限制接纳量；workspace 预留固定最大尺寸槽位以避免碎片。因此只调小字节预算不一定减少预留 RAM。参见[资源模型](resource-model.md)。

## 认证

启用认证能力的 profile 在 `auth_required` 为 true 时，要求应用提供 `auth_provider`，包括同步 sign/verify 回调以及 `1..XGL_AUTH_TAG_MAX_LEN` 的 tag 长度。发送认证流量前，用 `xgl_install_security_session()` 安装可信的双向参数。

认证不要求 libc 分配器，静态 workspace 同样支持。密钥、前缀、epoch 和重启后的新鲜性由应用负责。普通报文不能建立可信会话。关闭会话后保留 nonce 域的占用记录。

## 校验和编译 Profile

`boot` 允许一个 peer、最多一条路由、窗口一，不包含认证、分片、转发和乱序缓存。`embedded` 与 `full` 编译这些能力；实际启用项及容量仍由运行时配置决定。

压缩和 payload 加密仍是保留项，启用会被拒绝；两个 feature flag 保持 false，`compression_id` 保持零。通过 `xgl_memory_requirements()` 检查 workspace 算术溢出并查询依赖 ABI 的真实大小。库与消费者必须使用匹配的生成 profile 头文件。
