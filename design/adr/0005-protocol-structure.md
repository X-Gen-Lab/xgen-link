# ADR 0005：协议职责归并与私有边界

状态：已采用。日期：2026-10-01。

## 问题

逐函数拆文件造成 transport 的状态转换跨越过多文件；wire、认证和原地发送有重复规划与编码。每路由预留 link、datalink 内重复 RX 状态，以及层内存放安全上下文，使资源所有权难以追踪。公开 include 树混放私有头，测试根目录和构建入口也难以导航。

## 决策

生产库继续提供一个 `xgl::xgl` target。目录按协议职责划分，文件按状态生命周期和完整操作组织，不为几行包装创建文件，也不把所有 transport 合为一个大文件。

| 所有者 | 负责的状态或操作 |
| --- | --- |
| 实例 | 配置借用、workspace、唯一 security context、唯一 PHY 的 link 集合 |
| PHY link | 一个增量 parser、一个 RX cache、轮询时间 |
| transport peer | 精确 scope、窗口、可靠记录、RTT、RX 顺序和保留交付 |
| transport TX/RX/ACK/runtime | 分别负责接纳提交、顺序交付、反馈解释、重试与期限 |
| wire | 有界字段/TLV 规则、纯长度规划、编码与 CRC |
| security | provider 调用、认证帧生成、nonce 域、会话与重放 |
| datalink | 组帧、借用视图交付、唯一同步 PHY 提交出口 |
| network | 路由、逻辑包到帧的组合、本地认证、转发决策 |

普通、认证和原地发送共用帧规划。转发修改允许变化的字段后由 wire 更新 CRC，经 datalink 提交。接收使用栈上的已验证视图跨同步调用传递，不为每条 PHY 长驻完整视图，不再次计算完整帧 CRC。

可靠接纳直接返回记录指针。ACK 先校验整条反馈，再应用全部确认，最后尝试快重传。应用 BUSY 不释放已接纳消息；完整重组结果携带长度及明确预算所有权，统一 release 同时归还字节预算和存储。

`include/xgl` 只放公开头。其余头放 `src/<owner>`，跨层数据、资源和诊断契约放 `src/internal`。这些头不安装；内部测试显式添加私有 include 路径。生产源码、SDK 安装、开发检查分别由三个 CMake helper 管理，依赖版本仍由上层工程决定。

## 资源和兼容

Boot 编译关闭统计存储、计数更新及错误回调详细文字。统计 API 对有效实例返回 `XGL_ERR_UNSUPPORTED`，输出保持不变；错误回调保留错误码并传非空指针的空字符串。Full/Embedded 保留统计，RTT 观察不改变控制用估计器；内存统计表示完整 workspace 预留量。

wire v3、公开结构 ABI、同步 PHY 生命周期、可靠 scope 及接纳后 ACK 的语义保持。私有源码路径与内部 helper 不属于 SDK 兼容保证。共享组件不复制回本仓，也不新增生产子模块或隐式下载。

## 验证

行为变更先保存真实 RED，再运行生产实现取得 GREEN。保留全部 510 个历史 GoogleTest 名称和种子回放契约。模块测试、完整 profile、安装消费、独立 C11/C++17 头编译、静态分析、sanitizer 与最终 Cortex-M0 ELF 分别验证。

实际结果见[协议结构验证记录](../validation/protocol-structure.md)。最终 ELF 不等于板级 Bootloader；驱动、Flash 更新缓冲、认证实现和中断栈仍属于产品预算。
