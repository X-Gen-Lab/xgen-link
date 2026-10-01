# xgen-link SDK 3

具有显式资源上限的可移植 C11 协议。通用能力由独立的 xgen-status、xgen-bytes、xgen-crc、xgen-memory、xgen-containers 提供；准备方式见[构建与测试](getting-started/build-and-test.md)。

## 架构

静态初始化和 allocator 初始化共用工作区布局及状态机。应用提供时间、串行访问实例并实现同步 PHY 回调。wire v3 分离可靠 DATA 编号与认证安全序号。

## 阅读入口

- [构建与测试](getting-started/build-and-test.md)
- [快速开始](getting-started/quick-start.md)
- [迁移指南](guide/modular-migration.md)
- [公开 API](reference/public-api.md)
- [架构](protocol/architecture.md)
- [验证矩阵](reference/validation-matrix.md)

## 产品边界

Boot 裁剪认证、分片、转发及乱序缓存；Embedded/Full 提供这些有界能力。压缩、payload 加密和异步 DMA 所有权接口未实现。PHY TX 返回前必须消费或复制字节。板级接入与生产认证需要应用专属验证。
