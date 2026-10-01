# 架构

XGen Link 负责协议状态。xgen-memory 提供分配服务与固定池，xgen-containers 提供通用容器与位图，xgen-bytes、xgen-crc、xgen-status 分别提供字节编码、CRC 算法与通用状态。应用负责时钟、执行上下文、PHY 驱动与可信密钥建立。

## 模块边界

| 模块 | 职责 | 边界 |
| --- | --- | --- |
| API | 校验 ABI/profile/容量；构造实例；驱动执行 | 公开配置与显式时间戳 |
| Transport | 精确 peer 所有权；可靠 DATA；ACK；重试；可选消息层 | 有类型的逻辑包视图 |
| Network | 路由查找、TLV 组合、本地认证、转发 | 上层逻辑包，下层完整帧 |
| Security | 安装方向性 nonce 域、签名与重放状态 | 带版本的认证输入 |
| Datalink | 增量流组帧与同步 PHY 提交 | 借用字节区间 |
| Wire | 唯一规范帧解析器、序列化器与扩展编码 | 有界输入输出区间 |
| 协议内存 | 按协议生命周期预留资源 | 显式 xgm 分配服务 |

## 发送与接收流程

```mermaid
flowchart LR
    A[应用] --> T[Transport]
    T --> N[Network]
    N --> W[帧组合与认证]
    W --> D[Datalink]
    D --> P[同步 PHY]
```

接收按相反方向经过这些边界：每 PHY 的 parser 调用 wire 验证完整帧并生成借用视图，datalink 和 network 传递该视图；network 完成本地认证后，transport 才修改 peer 状态。完整帧 CRC 只计算一次。直接提交原始帧的内部入口仍先验证输入，不能冒充已验证视图。

普通、认证和原地发送共用帧长度规划与编码规则，统一由 datalink 提交 PHY。转发不创建本地 transport 状态，但也经过该 PHY 出口。可靠重传从持有的逻辑包重新组合帧，保留 DATA 编号并取得新的安全序号。

## 所有权与执行

一个 peer 唯一持有 `(remote_id, connection_id, session_epoch)` 对应的窗口、等待 ACK 记录、RTT 和接收顺序。借用的包/帧视图不得超出同步调用；需要保留的载荷由明确资源类别持有。安全状态独立于 peer 生命周期，不随 transport RESET 清除。

实例持有唯一 security context，各层借用它；每个唯一 PHY 描述符对应一个 parser 和 RX cache，共用 PHY 的多条路由不会重复预留。完整重组消息以含长度的自有对象移交，应用 BUSY 时继续占用原字节预算，直到统一释放。

同一实例由调用方串行访问。回调不得重入或销毁该实例。协议库不提供全局时钟、内部互斥包装、隐式堆回退或通用容器实现。参见[内存](memory.md)与[平台](platform.md)。

## 源码与构建边界

`include/xgl/` 仅保留公开 SDK。私有头与实现同放 `src/api`、`wire`、`datalink`、`network`、`transport`、`security`；跨层数据与资源契约放 `src/internal`。私有目录不安装、不导出给消费者。

生产仍导出一个 `xgl::xgl` target。`cmake/XglSources.cmake` 管理显式源码和 profile 裁剪，`XglInstall.cmake` 管理 SDK 包，`XglDevelopment.cmake` 管理开发检查。测试位于 `test/unit/<layer>`、`integration`、`property` 和 `support`，不改变历史测试名称与回放种子。
