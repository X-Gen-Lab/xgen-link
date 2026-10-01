# ADR 0007：复用帧布局与借用确认视图

状态：已采用。日期：2026-10-01。

## 问题

同步发送在 datalink 校验帧和目标容量后，序列化入口再次执行相同长度计算与 span 检查。size helper 使用独立加法，不能统一拒绝 wire 字段不可表示的长度。SACK 接收复制最多 64 字节位图到栈，再遍历副本。帧解码另保留临时逻辑头，头 CRC 为了将校验字段视为零而复制整个 24 字节固定头。

这些问题应从数据流和已有边界解决。减少文件或检查数量本身不是目标；新增长期缓存也不适合每路 PHY 都需要实例化的受限设备。

## 决策

### 有检查的长度入口

`xgl_frame_measure_lengths` 作为 payload、扩展、认证标签和完整帧长度的共同校验入口。size helper 在无法表示长度时返回零，payload budget 在失败时清零输出，并将成功结果限制在 `UINT16_MAX`。

`xgl_frame_measure` 在长度检查之外验证借用 span；认证发送仍拒绝调用者已有 SECURITY 扩展。`xgl_frame_encode_into`、`xgl_frame_serialize` 和 `xgl_security_serialize_frame` 保留各自的输入与容量检查。

### 同步发送中的布局复用

私有 `xgl_frame_encode_measured` 接收同次同步发送中成功测得的 layout。datalink 在检查原地偏移、容量或取得 scratch 后复用该 layout；认证发送通过私有 `xgl_security_serialize_measured` 继续签名并完成 CRC。

measured 入口不是输入验证边界。其前提是 frame、payload/TLV spans、标签长度和 layout 对应同一份未改变的数据，目标至少容纳 `layout.frame_len`。调用期间不得通过回调修改这些输入或重入实例。不能保存 layout 后用于下一帧，不能以自造 layout 绕过检查。这些入口不安装、不作为公开 SDK API。

编码、认证和 PHY 提交的职责保持分离。认证关联、广播限制、busy 与安全序号生命周期继续由 security 管理；完整帧在同步 PHY 返回之前有效。

### 借用 SACK 位图

`xgl_wire_decode_sack_view` 校验 SACK 的长度和字段边界，返回借用原 TLV 字节的视图；失败清零视图。旧复制接口使用同一解码规则，并继续在所有检查成功后才写调用者输出。

transport 使用该视图替代 64 字节栈数组，仍显式限制位图不大于 64 字节，拒绝空位图和超出已发送范围的确认。确认前先校验整条反馈，再处理确认和快重传。wire 视图能力不会隐式扩大 transport 的接受范围，也不会把位图指针保存到 peer 或可靠记录中。

### 解码临时存储与 CRC

固定头 CRC 通过独立 xgen-crc 的增量接口计算前 22 字节，再输入两个零字节。没有复制 CRC 实现，没有改变初值、多项式、字节序或 wire v3 校验范围。

完整帧解码直接填入输出 view 的 header，统一失败出口清零整个 view，省去临时头和最终复制。原始帧入口仍完整校验固定头、TLV 边界、标签长度、AUTHENTICATED 一致性和整帧 CRC。该轮 wire 优化未改变 parser 的长期状态、早期头检查、超时和重同步行为；后续改动见 [ADR 0008](0008-datalink-runtime.md)。

### 私有表示与文件职责

删除 `xgl_frame_params_t` 中未被帧构造器消费的 `data_type`、`session_epoch` 字段；两者仍由 network 组合为 DATA_TYPE、SESSION 扩展。私有零拷贝构造 helper 合入 `xgl_frame.c`，继续复用同一长度和编码逻辑，不保留独立包装文件。

该 helper 在本轮起点的 Boot ELF 中本来就没有链接，因此合并文件不记作 Flash 节省。普通帧构造函数仍有生产调用，不将其误归类为可直接删除的死代码。

## 未采用的方案

试验过 parser 完帧路径跳过第二次固定头 CRC 的私有入口。相对已精简 CRC 的相同 ELF，它增加 56 字节 Flash，新增路径和调用边界，也无法在不保存额外长期元数据的前提下消除第二次 TLV 解码。因此本轮不采用该方案。

不为每个 link 保存完整帧视图，不通过删除 CRC、长度检查或未知扩展边界检查换取体积。LTO 属于产品编译选择；跨文件内联后不能把残余符号大小简单相加当作 wire 的新占用。

## 兼容与资源

wire v3、四个公开头中已有结构 ABI、同步借用生命周期、外部串行化及可靠反馈语义保持。私有参数结构和内部头不属于 SDK 稳定接口。新增 checked helper 拒绝不可表示长度，不放宽原有接收限制。

没有新增 parser/peer 长期状态、分配服务或基础组件副本。最终资源以固定工具链、profile、容量和完整 ELF 报告为准；局部对象的变化不能机械相加作为整镜像收益。栈记录是单函数静态结果，不代表调用链、ISR 嵌套或板级高水位。

本轮本地 Cortex-M0、GCC 15.2.1、`-Os` 的完整 Boot 探针：关闭 LTO 为 15,932 B Flash、272 B 最大单函数栈；开启 LTO 为 12,600 B Flash、312 B 最大单函数栈。MTU/RX 128、64 分别使用 1,136 B、944 B workspace，含 1,024 B 预留栈的总 RAM 分别为 2,224 B、2,032 B。两种编译方式均未达到 8 KiB Flash 目标，不代表已上板。

## 验证

行为修复使用真实 RED/GREEN；视图新增接口包含编译 RED。RX 输出清零等既有契约先在旧实现上通过，再进行纯重构，不人为制造失败。

测试覆盖 wire 字节向量、非法长度、SACK 64/65 字节边界、错误反馈不部分提交、认证/原地发送、全部截断、CRC 分类及 parser 恢复。整合的主机、profile、安装、sanitizer、质量工具和 ARM 结果分别记录，不以主机通过代替远端 CI 或硬件验收。

使用说明与资源结果见[中文说明](../../docs/zh/reference/wire-optimization.md)和[英文说明](../../docs/en/reference/wire-optimization.md)；各编译器、配置、检查点与实际证据见[验证记录](../validation/wire-optimization.md)。
