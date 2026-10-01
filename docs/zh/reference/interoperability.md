# 互操作

SDK 3 仅使用 wire version 3，拒绝 version 2 帧；不提供自动降级或短 session 兼容路径。

## 必须一致的约定

对端必须约定 MTU、扩展语义、认证 tag 长度、可信 connection/epoch 和方向性安全参数。只有可靠 DATA 消耗可靠包序号。认证使用独立的 64 位安全序号及 provider 定义的安全算法。

## 证据

`test/test_wire.cpp`、`test/test_security.cpp`、`test/test_network.cpp` 和集成测试检查编码、认证输入、规范化转发 AAD 及路由交付。测试签名 provider 是确定性测试实现，不能用于生产密码。跨供应商密码互操作和板级 PHY 验证仍由应用负责。
