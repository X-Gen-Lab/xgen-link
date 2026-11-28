---
id: guides/advanced-runtime
type: how-to
audience: user
source_language: zh_CN
---

# 扩展运行选项

构建时设置 `-DXGL_ENABLE_ADVANCED_RUNTIME=ON`，安装消费可要求 `COMPONENTS advanced_runtime`。所有 profile 默认关闭；关闭时扩展入口返回 `XGL_ERR_UNSUPPORTED`。选项改变私有布局与构建身份，库和消费者必须使用同一生成配置头。高级运行时选项描述符的版本为 `2`。

## 创建与生命周期

```c
xgl_config_t config;
xgl_config_get_preset_boot(&config);
/* Populate routes, PHY callbacks and product identity before measurement. */
xgl_instance_options_t options = XGL_INSTANCE_OPTIONS_INIT;
options.tx_lifetime_ms = 1000U;
xgl_memory_report_t report;
xgl_error_t error = xgl_memory_report_with_options(&config, &options, &report);
```

检查返回值后，提供 `report.requirements.size` 字节且满足其 alignment 的存储，调用 `xgl_init_static_with_options()`。产品可在构建阶段测量并提供固定数组，也可用显式 allocator 调用 `xgl_create_with_options()`，随后调用 `xgl_init()`。静态入口已初始化，不要重复 init。

配置、options、路由、PHY、provider 与回调上下文都必须持续有效，直到销毁；不允许与 workspace 重叠。使用初始化宏填写尺寸、版本、配置 ABI 与构建身份，不硬编码。旧创建入口和旧 `xgl_step()` 保持可用。

## 期限与观察

| 选项或事件 | 含义 |
| --- | --- |
| `tx_lifetime_ms` | scope 可靠 TX 忙期上限；零关闭 |
| `rx_scoped_callback` | 优先于 accept/legacy 回调，携带完整接收 scope |
| `scope_event_callback` | 接收失败及可靠发送排空事件 |
| `XGL_SCOPE_TX_DRAINED` | 当前 scope 的待确认可靠包已全部确认 |
| `XGL_SCOPE_FAILED` | scope 已终止失败，错误码随事件提供 |

非零期限必须小于 `2^31` ms。TX 忙期在第一份可靠工作接纳时开始，持续到最后一个待 ACK 包确认；后续发送、BUSY、重试和部分 ACK 均不延长。到期会清理该 scope 拥有的工作并报告超时。持续发送的业务应选择足够的忙期上限，或以显式事务边界停止接纳新工作。

TX 忙期到期的 scope 在新 DATA 交付前终止，不能依靠接收阶段先于维护阶段来延长存活。

普通和原地实际发送均在提交前判定忙期。有效 ACK 完成语义校验后也先判定到期，再应用反馈。纯布局查询只准备缓冲区，实际发送重新判断期限。

发送排空是协议确认，不能作为远端电机执行、Flash 落盘或业务事务成功的证明。需要业务确认时应另加请求 ID 和业务响应。关闭 peer 是本地生命周期操作；同步通知、远端复位和重试策略由应用决定。

scoped 接收回调可返回 BUSY。若缓存中的乱序包已经由协议接纳，应用容量恢复后由 owner 再次调度交付。应用可主动唤醒 owner。所有回调同步执行，禁止重入同一实例或在回调中销毁实例。

## 资源快照

`xgl_resources_get()` 返回当前 peer 和失败 peer 数量、待 ACK 包及其载荷字节数、乱序包及其载荷字节数。读取不分配内存，调用方须与实例修改串行化。遍历开销与配置容量有关。

快照描述当前占用，`xgl_memory_report_with_options()` 测量整个 workspace 的预留。统计中的 `memory_used` 按预留 workspace 解释。对 RX cache，可显式设 `rx_buffer_size = max_frame_size` 以节省存储。

## 安全 scope 策略

有认证能力的 profile 可设 `protect_installed_scopes=true`。它要求合法 provider 和可容纳基本认证帧的协议 MTU。先为 exact `(remote_id, connection_id, epoch)` 安装可信安全会话，再发送数据；该 scope 的 HELLO、DATA、ACK、SACK、RESET、重传及零拷贝布局均采用认证策略。

关闭后的 scope 不回退为明文。其他未安装 scope 在全局 `auth_required=false` 时仍可使用明文；这不是自动协商或动态信任建立。全局 required 继续强制所有本地收发认证。发送预检、原地布局与实际提交按目标的精确 scope 计算帧大小，仅为须认证的帧计入 SECURITY 和 tag。

安装不撤销之前已经接纳的数据；先前缓存的明文仍可在安装后交付。需要新的信任边界时，使用新 epoch/scope 并先安装后接收，显式处置旧 scope 的数据。详见[安全生命周期](../reference/protocol/v1/security.md)。

关闭安全会话保留 nonce 域记录；不能无限安装并关闭会话来回收槽。产品负责密钥轮换、epoch、重启新鲜性、真实密码实现及其验证。认证不等于 payload 加密。

## 调度与验证

执行预算、pending 和 DMA owner 示例见[有界接收与运行](../explanation/runtime.md)。预算限制工作次数，不能替代完整 WCET 测量。旧 step 在扩展构建中使用无限候选和维护配额，不应在需要有界运行时混用。

主机 GoogleTest 验证行为；每个 profile 的实际 ELF、栈、DMA cache 一致性、中断延时与产品内存预算需分别验收。测量时构建实际调用扩展 API 的产品固件。
