---
id: guides/boot
type: how-to
audience: user
source_language: zh_CN
---

# Boot 接入

Boot 编译档位保留静态 workspace、单 peer、最多一条路由和包窗口 1。它支持可靠单播，应用自行分块。认证、转发、乱序、统计和详细诊断文字由其他档位提供。

## 选择资源

用 [编译档位](../reference/profiles.md) 选择构建，再用 `xgl_config_get_preset_boot()` 设置运行配置。设置路由、节点、MTU、RX 缓冲和时间预算，通过 `xgl_memory_requirements()` 获取真实大小和对齐。将存储配置为静态并以 `xgl_init_static()` 初始化。包载荷预算按真实头、TLV 和 CRC 扣除后计算。

测量目标固件并记录外部 PHY 所需的缓冲、时序及实际调用栈。运行窗口 1 与独立的编译窗口上限分别选择。资源证据参见[验证](../development/validation.md)。

## 接收和提交

从[可运行示例](../tutorials/examples.md)查看主机 `boot_update`。应用接纳回调在当前块无法写入暂存槽时返回 `XGL_ERR_BUSY`。接纳时复制数据到自己的槽，由 owner 在回调退出后推进 Flash 操作并发送提交回执。发送端应等这一回执后推进镜像偏移。复位或错误之后，排空旧流量并按新的 scope 恢复。

将驱动接纳、可靠 ACK、持久化提交和镜像启用分开记录。真实产品提供 Flash 分区、已签名镜像验证、版本策略、断电恢复与启动交接。应用共享一个 PHY 时保持单一执行者和显式借用边界。静态协议 workspace 之外还需核算 BSP、暂存区与安全组件。

## 验收

验证背压、丢 ACK、字节流中断、时钟回绕、资源耗尽和停止。主机模拟覆盖传输和接纳。板级 Flash、DMA 与安全验收另按产品要求执行。
