# SDK 3 模块化迁移

SDK 3 明确移除了通用工具兼容层和隐式平台运行时。消费者必须与新头文件、库一起重新构建。

## 构建和依赖

协议消费五个独立 C11 包，各包当前版本为 0.1.0，支持范围为 `>=0.1.0,<0.2.0`、ABI 1：

| 包 | 协议使用的 targets | 仅 dev 使用的源码入口 |
| --- | --- | --- |
| xgen_status | `xgs::status` | `XGL_DEV_STATUS_SOURCE_DIR` |
| xgen_bytes | `xgb::bytes` | `XGL_DEV_BYTES_SOURCE_DIR` |
| xgen_crc | `xgcrc::crc16` | `XGL_DEV_CRC_SOURCE_DIR` |
| xgen_memory | `xgm::allocator`、`xgm::pool`、`xgm::size_class`；可选 `xgm::libc_allocator` | `XGL_DEV_MEMORY_SOURCE_DIR` |
| xgen_containers | `xgct::list`、`xgct::bitset`；启用路由索引时使用 `xgct::hash` | `XGL_DEV_CONTAINERS_SOURCE_DIR` |

正式 CMake 入口只接受父工程已经提供的兼容 targets 或通过 `find_package` 取得的安装包。产品顶层选择每个镜像的唯一依赖组合；link 已移除五个 `external` 子模块，不递归检出第二套生产组件。各入口都检查版本、ABI 和 target 类型；配置过程不下载依赖，也不搜索基础组件源码目录。

原 `XGL_{STATUS,BYTES,CRC,MEMORY,CONTAINERS}_SOURCE_DIR` 已退出生产入口，继续传入会明确报错。独立源码开发改用 `cmake -S dev` 及上表五个 `XGL_DEV_*_SOURCE_DIR` 参数；不能只改变量名而仍配置根目录。`dev/dependencies.json` 固定本仓开发/CI 测试输入，不控制产品版本。详见[构建与测试](../getting-started/build-and-test.md)及仓库 `dev/README.md`。

正式模块作为产品子目录时默认关闭示例、smoke 和发布辅助；dev 装配显式启用开发检查。旧构建缓存可能保存已移除的路径和辅助开关，应使用新的构建目录。

安装入口按包判断预提供集合：所需 targets 全部存在时直接复用并执行相同兼容检查，不因旧安装 metadata 拒绝完整的兼容集合；只提供一部分时仍加载该包，由提供者精确身份检查拒绝跨版本拼接。

同一包的全部 targets 必须使用同一版本，源码与安装入口共用该检查；例如 memory 的 allocator 0.1.0 与 pool 0.1.1 不能混用。不同包可分别选择其兼容范围内的版本。

旧 core 聚合包、`XGL_CORE_SOURCE_DIR` 和 `xgc::*` 不再是消费入口。公开 allocator 改为 `<xgen/memory/allocator.h>` 中的 `xgm_allocator_t`；内部容器、字节和 CRC 分别使用 `xgct_`、`xgb_`、`xgcrc_`。不提供旧前缀或 xgl 工具别名。`xgl_error_t` 保留协议错误域，通用状态由 `xgs_status_t` 提供。status strings、CRC8、arena、tracking、ring_buffer 不因协议消费自动引入；协议 PHY 仍为同步契约。

## 编译 Profile

通过 CMake 选择 `XGL_PROFILE=boot|embedded|full`。生成的 profile 头文件、ABI/build 标识必须与链接库一致。运行时可以关闭编译能力，不能恢复已被所选 profile 裁掉的能力。

Boot 在编译时去掉可选字段和代码。压缩/加密 registry、内部 mutex、全局时钟和旧 layer-vtable 分发都不再作为 SDK 扩展点。

## Workspace 迁移

将旧 TX pool 和 tiered pool 配置替换为显式 peer、包、消息和重组容量。查询 `xgl_memory_requirements()`，使用 `xgl_init_static()` 接入调用者存储。动态创建使用带 `ctx` 的 `const xgm_allocator_t*`，只向 backend 申请一次。

配置改为借用，不再复制。创建函数返回后失效的局部配置不合法。配置及其引用描述符必须保持不可变并活到 destroy。

## 运行时和所有权

将隐式时钟的 send/run 替换为 `xgl_send_at()`、`xgl_send_zerocopy_at()`、`xgl_step()` 和 `xgl_next_timeout()`。统一传入单调的 32 位毫秒时间，并由外部串行化访问。

PHY TX 是同步接口，返回后不得保留指针。异步 DMA 驱动必须先复制到驱动自有存储再返回。接收回调只在执行期间借用字节；需要显式接纳或背压时使用 `rx_accept_callback`。任何回调都不能重入或销毁所属实例。

## Scope 和安全迁移

可靠状态归属于完整的 remote/connection/epoch scope。失败或显式关闭后，排空未认证链路旧流量，以新 epoch 重连。认证 peer 通过 `xgl_close_security_session()` 关闭，可信双向密钥及 nonce 域必须更新且不可复用。

认证 provider 使用 `xgl_auth_input_t`，包含实际 wire 身份、nonce、AAD 和 payload。不能继续使用只签 payload 的旧 provider。请结合协议安全文档，独立验证可信会话建立流程。
