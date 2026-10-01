# 实现映射

本页标明协议状态的所有者。公开应用包含 `<xgl/xgl.h>`；内部头文件用于实现边界，不作为已删除通用工具的兼容别名。

## 源码所有权

| 契约 | 主要源码 |
| --- | --- |
| ABI 校验、生命周期、公开 step | `src/api/` |
| 静态工作区规划与资源服务 | `src/api/xgl_workspace.c` |
| 精确 peer 生命周期与 close | `src/transport/xgl_transport_peer.c` |
| ACK 原子校验与应用 | `src/transport/xgl_transport_ack.c` |
| 期限汇总与周期处理 | `src/transport/xgl_transport_runtime.c` |
| 生产重传路径 | `src/transport/xgl_transport_runtime.c` |
| 自有消息推进 | `src/transport/xgl_transport_tx_message.c` |
| 接收排序与保留投递 | `src/transport/xgl_transport_rx.c`、`xgl_transport_rx_delivery.c` |
| 分片覆盖与重组 | `src/transport/xgl_fragment_*.c` |
| 路由查找、本地投递与转发 | `src/network/` |
| 规范帧布局与 TLV | `src/wire/` |
| 方向性会话状态与认证 | `src/security/` |
| 增量组帧与同步 PHY | `src/datalink/` |

## 内部接口

`xgl_protocol_io.h` 使用有类型参数区分 packet 与 frame 接口。Transport 提供 receive 接口，并向 network 提交逻辑包。`xgl_packet.h` 包含借用的协议视图。`xgl_protocol_memory.h` 描述资源类别服务，不实现分配器。

可靠队列保存并索引记录，不独立确认 scope，也不直接向 PHY 重发原始帧。由 peer 所有者 transport 经当前 network/security 路径执行这些状态转换。

## 验证边界

单元测试覆盖 ACK 校验、scope 隔离、取消、自有载荷生命周期与容量拒绝。Transport 属性测试经过生产重传路径。静态工作区属性测试在有界资源服务上验证跨窗口分片和接收背压。Wire/security 测试断言字节布局、nonce/AAD 构造与重放行为。SDK/profile 冒烟构建独立于内部单元夹具，检查安装后的公开 API。
