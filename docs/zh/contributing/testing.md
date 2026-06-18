# 测试策略

本文档描述 XGL 协议栈的测试架构、各类测试的设计意图和覆盖策略。

## 测试分层

```text
┌─────────────────────────────────────────┐
│ Integration Tests (1 文件)               │
│  端到端协议栈行为验证                      │
├─────────────────────────────────────────┤
│ Property-Based Tests (11 文件)           │
│  不变量验证、模糊输入、边界条件              │
├─────────────────────────────────────────┤
│ Unit Tests (30+ 文件)                    │
│  各模块独立功能验证                        │
├─────────────────────────────────────────┤
│ Mocks (3 对文件)                         │
│  PHY、回调、分配器的可控替代                │
└─────────────────────────────────────────┘
```

## Property-Based Testing

XGL 使用自定义 property-based 测试框架(`test/property/property_framework.h`),通过随机输入验证协议栈的不变量。

### 测试文件与验证的不变量

| 文件 | 验证的不变量 |
| --- | --- |
| `test_alignment_properties.cpp` | 内存对齐:所有结构体在目标对齐边界上正确访问 |
| `test_crc_properties.cpp` | CRC 计算:相同数据始终产生相同 CRC;不同数据产生不同 CRC |
| `test_error_properties.cpp` | 错误处理:所有 API 在非法参数下返回明确错误码,不崩溃 |
| `test_fragment_properties.cpp` | 分片重组:任意分片顺序重组后数据一致;超时正确清理 |
| `test_frame_properties.cpp` | 帧编解码:encode → decode 往返一致;字段边界正确处理 |
| `test_instance_properties.cpp` | 实例生命周期:create → run → destroy 无泄漏;重复 init 安全 |
| `test_memory_properties.cpp` | 内存分配:alloc/free 配对;pool 耗尽返回 NULL;峰值统计正确 |
| `test_network_properties.cpp` | 网络层:路由查找正确;TTL 递减;转发 CRC 重算 |
| `test_serialization_properties.cpp` | 序列化:TLV 编解码往返一致;边界长度正确处理 |
| `test_transport_properties.cpp` | 传输层:可靠发送 ACK 后释放;超时重传;窗口满阻塞 |

### Property 测试模式

每个 property 测试遵循:

1. **定义不变量**:描述期望成立的条件。
2. **生成随机输入**:使用确定性种子生成随机参数。
3. **执行操作**:在随机输入上执行协议操作。
4. **验证不变量**:断言不变量在操作后仍然成立。
5. **记录种子**:失败时记录随机种子,可精确复现。

## Mock 设计

### mock_phy

模拟物理层收发,用于在无硬件环境下测试 datalink 和 network 层:

- `mock_phy_init()`:初始化 mock PHY,配置发送/接收缓冲区。
- `mock_phy_get_tx_buffer()`:获取发送的数据,用于验证帧格式。
- `mock_phy_enqueue_rx()`:注入接收数据,模拟远端发送。
- `mock_phy_reset()`:重置状态。

### mock_callbacks

模拟应用层回调:

- `mock_rx_callback`:记录收到的数据,用于验证交付正确性。
- `mock_error_callback`:记录错误,用于验证错误处理。

### mock_allocator

模拟内存分配器:

- `mock_allocator_init()`:初始化,可配置分配失败点。
- `mock_allocator_set_fail_after()`:设置第 N 次分配后失败,测试内存耗尽路径。
- `mock_allocator_get_alloc_count()`:查询分配次数。

## 集成测试

`test/integration/test_integration.cpp` 验证完整协议栈的端到端行为:

1. 创建实例 + 配置路由 + 注册回调。
2. 通过 mock PHY 注入接收数据。
3. 调用 `xgl_run()` 驱动协议栈。
4. 验证应用层收到正确的数据。
5. 验证统计计数器正确。

## 测试构建

```bash
# 构建测试
cmake --preset dev
cmake --build build-dev --target xgl_tests

# 运行测试
ctest --test-dir build-dev --output-on-failure

# 运行特定测试
./build-dev/test/xgl_tests --gtest_filter="TestName"
```

## 覆盖率

构建时启用覆盖率:

```bash
cmake --preset dev -DENABLE_COVERAGE=ON
cmake --build build-dev
ctest --test-dir build-dev
gcovr build-dev --root .
```

## 证据

| 组件 | 源码 | 测试 |
| --- | --- | --- |
| Property framework | `test/property/property_framework.h` | `test/property/test_*.cpp` |
| Mock PHY | `test/mocks/mock_phy.cpp` | `test/integration/test_integration.cpp` |
| Mock callbacks | `test/mocks/mock_callbacks.cpp` | `test/integration/test_integration.cpp` |
| Mock allocator | `test/mocks/mock_allocator.cpp` | `test/integration/test_integration.cpp` |