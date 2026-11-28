---
id: explanation/architecture
type: explanation
audience: user
source_language: zh_CN
---

# 架构

XGen Link 是独立 C11 协议库，负责线上编码、链路处理与可靠状态。协议所需的固定池、队列、位图、字节编码及 CRC 随生产库构建。应用负责时钟、执行上下文、PHY 驱动与可信密钥建立。

## 模块边界

| 模块 | 职责 | 边界 |
| --- | --- | --- |
| API | 校验 ABI/profile/容量；构造实例；驱动执行 | 公开配置与显式时间戳 |
| Transport | 精确 peer 所有权；可靠 DATA；ACK；重传；可选乱序缓存 | 有类型的逻辑包视图 |
| Network | 路由查找、TLV 组合、本地认证、转发 | 上层逻辑包，下层完整帧 |
| Security | 安装方向性 nonce 域、签名与重放状态 | 带版本的认证输入 |
| Datalink | 增量流组帧与同步 PHY 提交 | 借用字节区间 |
| Wire | 唯一规范帧解析器、序列化器与扩展编码 | 有界输入输出区间 |
| 协议内存 | 按协议生命周期预留资源 | 私有固定资源与可选公开分配回调 |

## 发送与接收流程

![同步发送路径](../diagrams/send-receive.svg)

接收按相反方向经过这些边界：每 PHY 的 parser 调用 wire 验证完整帧并生成借用视图，datalink 和 network 传递该视图；network 完成本地认证后，transport 才修改 peer 状态。完整帧 CRC 只计算一次。直接提交原始帧的内部入口仍先验证输入，不能冒充已验证视图。

普通、认证和原地发送共用帧长度规划与编码规则，统一由 datalink 提交 PHY。转发不创建本地 transport 状态，但也经过该 PHY 出口。可靠重传从持有的逻辑包重新组合帧，保留 DATA 编号并取得新的安全序号。

## 所有权与执行

一个 peer 唯一持有 `(remote_id, connection_id, session_epoch)` 对应的窗口、等待 ACK 记录、RTT 和接收顺序。借用的包/帧视图不得超出同步调用；需要保留的载荷由明确资源类别持有。安全状态独立于 peer 生命周期，不随 transport RESET 清除。

实例持有唯一 security context，各层借用它；每个唯一 PHY 描述符对应一个 parser 和 RX cache，共用 PHY 的多条路由不会重复预留。已缓存的乱序包在应用 BUSY 时继续占用接收槽，直到交付或所属 scope 关闭或失败。

同一实例由调用方串行访问。回调不得重入或销毁该实例。时钟和实例调度由应用提供。工作区固定，运行阶段只消耗已预留资源。参见[内存](resource-model.md)与[平台](../guides/porting.md)。

## 源码与构建边界

`include/xgl/` 仅保留公开 SDK。私有头与实现同放 `src/api`、`wire`、`datalink`、`network`、`transport`、`security`；跨层数据与资源契约放 `src/internal`。私有目录不安装、不导出给消费者。

生产导出一个 `xgl::xgl` target。`cmake/XglVersion.cmake` 为库与独立文档提供版本，`XglProfiles.cmake` 解析档位与资源身份，`XglSources.cmake` 按能力列出生产编译单元，`XglLibrary.cmake` 组装带匹配配置头的库。`XglInstall.cmake` 导出 SDK，`XglDevelopment.cmake` 接入按需启用的开发检查。

协议回归位于 `test/unit/<layer>`、`test/integration` 和 `test/property`。`test/smoke` 保存 C11 夹具，`test/interop` 保存 Wire v1 双端点夹具，`test/contracts` 按构建、安装与 CTest 区分验证对象。历史用例和回放种子由现有发现入口维护。
