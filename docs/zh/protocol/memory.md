# 内存与容量

内存策略必须显式给出。协议代码使用包含分配、释放和调用方上下文的 `xgm_allocator_t` 服务；空服务不表示隐式使用 libc。宿主应用可以显式选择独立的 xgen-memory libc 分配器。静态应用提供对齐工作区，并在初始化前查询精确大小。

## 资源所有者

| 资源类别 | 所有者与生命周期 |
| --- | --- |
| Peer、窗口与可选 RTT | 精确 scope；开始使用可靠编号后保留至显式 close |
| TX 记录与载荷 | 单个已接纳可靠包，直到 ACK、scope 失败或 close |
| TX 分片扩展 | 与可靠分片一起保留的元数据 |
| TX 消息 | 每个 peer 一个待发送完整消息副本，直到各片进入可靠记录 |
| RX 记录与连续载荷/TLV 存储 | 已接纳乱序包，直到投递或 scope 终止 |
| 重组记录与载荷 | 未完成消息；完整载荷可转交给 peer 的待投递存储 |
| Scratch | 一次同步帧组合或转发操作 |

`xgl_protocol_memory_t` 为各类别绑定显式服务，分配与释放必须使用相同类别。借用包视图只含载荷长度和 const 字节指针，不暗示引用计数所有权。分片管理器内嵌在 transport 上下文中。

窗口确认位图内嵌在 peer 的窗口对象中，没有独立窗口分配器；公开内存报告中的 `window_bytes` 保留且为零，实际存储计入 `peer_bytes`。可靠索引桶数、编译窗口上限和可选 RTT 状态共同决定 peer 尺寸，须使用当前构建身份对应的报告；配置方式见[构建与测试](../getting-started/build-and-test.md)。

## 显式容量契约

`max_peers` 和 `max_tx_packets` 必须非零。TX 记录上限由全部 peer 共享。窗口为一时 `max_rx_buffered_packets` 可以为零；更大的窗口需要正数接收缓冲容量，该上限同样由全部 peer 共享。

启用分片时，`max_message_size`、`max_reassembly_slots`、`max_reassembly_bytes` 和 `max_tx_message_bytes` 必须为正。消息大小不能超过 `UINT32_MAX`，因为 FRAGMENT_EXT 使用 32 位总长度。重组字节预算包括未完成消息和因应用背压保留的完整消息。TX 消息字节预算涵盖全部待发送完整副本。

每条活跃重组记录保存已接纳消息的可靠属性，不能从 peer 曾有可靠 RX 历史推导。混用可靠与非可靠分片在接纳前拒绝；ACK 提交失败不改变已有属性。超时根据消息属性决定仅释放消息还是终止所属 scope，详见[可靠传输](reliability.md)。

容量耗尽时拒绝新的接纳，不得假装字节已接纳。已接纳字节必须持续有所有者，直到完成或可观察的失败。Scratch 独立预留，因此占满 DATA 存储不会占用 ACK/控制帧的序列化容量。

## 静态工作区布局

使用 `xgl_memory_requirements` 查询后，再以匹配 ABI、profile 和编译资源身份的公开头文件调用 `xgl_init_static`。规划器检查对齐与算术溢出，拒绝不足的存储。各类别固定池由 xgen-memory 实现，协议不重复实现空闲链表。

MTU 为 `M` 时，静态 TX 载荷槽最多预留 `M - 26` 字节。每个乱序 RX 节点拥有一个连续的载荷加 TLV 存储块，合计最多 `M - 26` 字节；两个借用视图指向同一个块，释放一次，没有独立 RX 扩展槽。同步、禁止重入的契约下，scratch 预留一个 `M` 字节槽。TX 分片元数据每条 TX 记录预留 14 字节。启用分片后，为 peer 预留完整 TX 消息槽，为未完成消息与 peer 持有的完整 RX 消息预留重组载荷槽；字节接纳预算仍独立生效。应查询精确工作区，不能只累加载荷上限：实例、解析器、路由、索引、池元数据与对齐也占 RAM。

编译窗口上限决定确认位图的容量；运行时 `protocol.window_size` 决定实际工作窗口，还须满足公开 API 的上限。缩减编译容量后，超出该容量的运行时配置在初始化前拒绝；不能通过覆盖生成头宏扩大已编译对象。关闭自适应 RTO 会裁掉 peer 的估计器与采样路径，但不会取消可靠记录、ACK 或退避。

验证依据：`test/unit/transport/test_transport_storage.cpp`、`test/unit/transport/test_transport_fragment_scope.cpp`、`test/unit/transport/test_transport_fixed_rto.cpp`、`test/property/test_memory_properties.cpp` 与 `test/cmake/test_transport_resource_config.py`。工作区查询与主机测试结果不代替完整目标 ELF、应用栈与板级预算验收。
