# 验证矩阵

验证矩阵把协议能力、风险和测试目标关联起来。发布前应确保每一行都有自动化测试或明确的人工验证记录。

## 核心协议矩阵

| 能力 | 风险 | 必须验证 | 推荐测试位置 |
| --- | --- | --- | --- |
| v2 base header | offset 错误、大小端错误、CRC 覆盖错误 | 24-byte offset、little-endian、CRC 字段置零计算 | `test/test_wire.cpp`, `test/test_frame.cpp` |
| TLV cursor | 越界、零长度误判、未知扩展处理错误 | 多扩展、空扩展、非法 length、header_len 越界 | `test/test_wire.cpp`, `test/test_parser.cpp` |
| parser resync | 噪声导致卡死或错帧 | 噪声、重叠 magic、分片输入、连续多帧 | `test/test_parser.cpp` |
| auth trailer | 未认证帧穿透、tag 长度错配 | auth_required 缺 provider、伪造 header/payload/tag、zero-copy auth | `test/test_security.cpp`, `test/test_datalink.cpp`, `test/test_send.cpp` |
| replay window | 重放攻击、跨连接污染 | source/connection/session/packet 隔离，重复包拒绝 | `test/test_security.cpp`, `test/test_datalink.cpp` |
| route forwarding | TTL/auth AAD 冲突、MTU 超限 | TTL 递减、CRC/auth 重签、route MTU 拒绝 | `test/test_network.cpp` |
| reliable queue | ACK 释放错误、SACK 洞丢失 | ACK range 批量释放、SACK 快速重传、retry limit | `test/test_transport.cpp`, `test/test_reliable.cpp` |
| peer state | 多连接互相污染 | peer key 按 node/connection/session 隔离 | `test/test_transport.cpp` |
| ordered delivery | 乱序重复交付 | out-of-order 缓存、连续推进、重复包过滤 | `test/test_transport.cpp` |
| fragmentation | 内存耗尽、跨 session 混包 | FRAGMENT_EXT 重组、预算、timeout、reset scope | `test/test_fragment.cpp` |
| low-power deadline | 睡眠过久导致超时 | route/reliable/reassembly 最近 deadline | `test/test_instance.cpp` |
| noheap profile | 隐式 malloc、碎片化 | noheap smoke、allocator 失败路径 | `tools/noheap_smoke.c`, memory tests |

## Fuzz / Stress 建议

| 场景 | 输入模型 | 通过标准 |
| --- | --- | --- |
| parser random bytes | 随机 byte stream，插入合法/半合法 frame | 不崩溃，不越界，能恢复到下一合法 magic |
| TLV malformed | 随机 ext_type/ext_len/header_len | 非法 TLV 丢弃，合法 TLV 正确解析 |
| auth tamper | 修改 header、extension、payload、tag 任意字节 | auth_required 下全部拒绝 |
| route storm | 多节点 route 切换、TTL 边界、MTU 边界 | 不转发 TTL 过期帧，不发送超 MTU 帧 |
| lossy transport | loss/reorder/duplicate/delay 注入 | 可靠包最终有序交付或按 retry limit 失败 |
| fragment attack | 大 message、重叠 range、缺片、超时 | 预算不被突破，超时释放资源 |

## Release Gate

推荐顺序：

```sh
cmake --preset gcc-test
cmake --build build/gcc-test --target xgl_tests
ctest --preset gcc-test --output-on-failure
cmake --build build/gcc-test --target xgl_release_validation
cmake --preset ci
cmake --build build/ci --target xgl_docs
```

发布环境必须安装 `cppcheck`。静态分析 unavailable 不是通过条件。

## 文档一致性检查

- 文档中的 node id 必须是 `uint16_t`。
- 文档中的 packet number 必须是 `uint32_t`。
- 文档中的 wire header 必须是 v2 24-byte header。
- 未实现能力必须写 reserved，并说明 production path 会拒绝或不启用。
- 公共 API 文档只描述稳定 SDK 入口，不把内部状态结构承诺为 ABI。
