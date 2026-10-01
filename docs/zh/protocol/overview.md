# 协议概览

XGen Link 实现面向有界、同步嵌入式通信的 wire version 3。各 profile 使用相同的帧头和扩展编码。配置明确拒绝不支持的能力，不对 version 2 帧进行隐式升级或降级。

## 职责

| 组件 | 职责 |
| --- | --- |
| API | 实例生命周期、显式时间、配置与工作区检查 |
| Transport | peer 所有权、可靠 DATA、ACK/SACK、重试与可选消息层 |
| Network | 目标查找、扩展组合与可选转发 |
| Security | 显式可信会话、独立认证序号与重放检查 |
| Datalink / Wire | 同步帧提交、有界解析、布局与 CRC |
| 独立基础包 | xgen-memory 分配/池；xgen-containers 容器/位图；xgen-bytes 字节序；xgen-crc 校验；xgen-status 通用状态 |

## 核心契约

peer 严格由 `(remote_id, connection_id, session_epoch)` 标识。connection 和 epoch 的零值是普通取值，不存在短 session 回退。只有可靠 DATA 消耗连续的 32 位 DATA 序号；ACK、CONTROL、非可靠 DATA 的该帧头字段为零。每次签名尝试使用独立的 64 位认证序号。

可靠发送成功表示本地已接纳并持有数据。ACK 表示 transport 或应用已接纳，不代表写 flash 或其他业务操作已经提交。失败对该 epoch 是终态。RESET 不会将其重新开放；显式 close 释放容量后，才能建立新 epoch。

## 阅读顺序

先阅读[架构](architecture.md)、[线格式](wire-format.md)、[扩展](extensions.md)、[可靠传输](reliability.md)和[安全](security.md)。[分片](fragmentation.md)、[内存](memory.md)、[平台](platform.md)与[实现映射](implementation-map.md)说明容量、执行及源码所有权。Boot profile 使用单 peer、单包窗口；固件传输的分块属于 Boot 应用协议。
