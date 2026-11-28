---
id: guides/advanced-runtime
type: how-to
audience: user
source_language: zh_CN
---

# 扩展运行选项

构建时设置 `-DXGL_ENABLE_ADVANCED_RUNTIME=ON`，安装消费可要求 `COMPONENTS advanced_runtime`。所有 profile 默认关闭。关闭时 `xgl_step_bounded()` 及非空 `config.options` 返回 `XGL_ERR_UNSUPPORTED`。库和消费者必须使用同一生成配置头。`<xgl/xgl_runtime.h>` 声明版本为 `5` 的策略描述符。

## 创建与生命周期

```c
#include <xgl/xgl_runtime.h>

xgl_instance_options_t options = XGL_INSTANCE_OPTIONS_INIT;
options.tx_lifetime_ms = 1000U;
xgl_config_t config = {
    .source_id = 1U,
    .memory = {.rx_buffer_size = 64U},
    .protocol = {.ack_timeout_ms = 200U, .max_retry_count = 3U,
                 .window_size = 1U, .max_frame_size = 64U},
    .features = {.max_tx_packets = 1U, .max_peers = 1U},
    .options = &options,
};
/* Populate routes, PHY callbacks and product identity before measurement. */
xgl_memory_requirements_t required;
xgl_error_t error = xgl_memory_requirements(&config, &required);
```

检查返回值后，提供 `required.size` 字节并满足 `required.alignment`，再调用 `xgl_init_static()`。也可包含 `<xgl/xgl_alloc.h>`，通过 `xgl_init_allocated()` 申请工作区并完成初始化。两种方式读取同一个 `config.options`。

配置、options、路由、PHY、provider 与回调上下文都必须持续有效，直到销毁。配置和工作区不能重叠。使用初始化宏填写描述符尺寸、版本和配置尺寸。`xgl_step()` 与 `xgl_step_bounded()` 可以推进同一实例。

## 期限与观察

| 选项或事件 | 含义 |
| --- | --- |
| `tx_lifetime_ms` | scope 可靠 TX 忙期上限；零关闭 |
| `scope_event_callback` | 接收失败及可靠发送排空事件 |
| `XGL_SCOPE_TX_DRAINED` | 当前 scope 的待确认可靠包已全部确认 |
| `XGL_SCOPE_FAILED` | scope 已终止失败，错误码随事件提供 |

非零期限必须小于 `2^31` ms。TX 忙期在第一份可靠工作接纳时开始，持续到最后一个待 ACK 包确认；后续发送、BUSY、重试和部分 ACK 均不延长。到期会清理该 scope 拥有的工作并报告超时。持续发送的业务应选择足够的忙期上限，或以显式事务边界停止接纳新工作。

TX 忙期到期的 scope 在新 DATA 交付前终止，不能依靠接收阶段先于维护阶段来延长存活。

普通和原地实际发送均在提交前判定忙期。有效 ACK 完成语义校验后也先判定到期，再应用反馈。纯布局查询只准备缓冲区，实际发送重新判断期限。

发送排空是协议确认，不能作为远端电机执行、Flash 落盘或业务事务成功的证明。需要业务确认时应另加请求 ID 和业务响应。关闭 peer 是本地生命周期操作；同步通知、远端复位和重试策略由应用决定。

核心 `config.rx_callback` 接收完整 scope，可返回 BUSY。若乱序包已经进入协议缓存，应用容量恢复后可唤醒 owner 再次调度交付。所有回调同步执行，禁止重入同一实例或在回调中销毁实例。

## 资源快照

`xgl_resources_get()` 返回当前 peer 和失败 peer 数量、待 ACK 包及其载荷字节数、乱序包及其载荷字节数。读取不分配内存，调用方须与实例修改串行化。遍历开销与配置容量有关。

在 `<xgl/xgl_diagnostics.h>` 中，`xgl_resources_get()` 描述当前占用，`xgl_memory_report()` 读取同一配置并测量整个工作区的预留。统计中的 `memory_used` 按预留工作区解释。对 RX cache，可显式设 `rx_buffer_size = max_frame_size` 以节省存储。

## 安全 scope 策略

有认证能力的 profile 可设 `protect_installed_scopes=true`。它要求合法 provider 和可容纳基本认证帧的协议 MTU。先为 exact `(remote_id, connection_id, epoch)` 安装可信安全会话，再发送数据；该 scope 的 HELLO、DATA、ACK、SACK、RESET、重传及零拷贝布局均采用认证策略。

关闭后的 scope 不回退为明文。其他未安装 scope 在全局 `auth_required=false` 时仍可使用明文；这不是自动协商或动态信任建立。全局 required 继续强制所有本地收发认证。发送预检、原地布局与实际提交按目标的精确 scope 计算帧大小，仅为须认证的帧计入 SECURITY 和 tag。

安装不撤销之前已经接纳的数据；先前缓存的明文仍可在安装后交付。需要新的信任边界时，使用新 epoch/scope 并先安装后接收，显式处置旧 scope 的数据。详见[安全生命周期](../reference/protocol/v1/security.md)。

关闭安全会话保留 nonce 域记录；不能无限安装并关闭会话来回收槽。产品负责密钥轮换、epoch、重启新鲜性、真实密码实现及其验证。认证不等于 payload 加密。

## 调度与验证

执行预算、pending 和 DMA owner 示例见[有界接收与运行](../explanation/runtime.md)。预算限制工作次数，产品仍需测量完整 WCET。需要执行配额时调用 `xgl_step_bounded()`，因为普通 `xgl_step()` 不限制候选和维护次数。

主机 GoogleTest 验证行为；每个 profile 的实际 ELF、栈、DMA cache 一致性、中断延时与产品内存预算需分别验收。测量时构建实际调用扩展 API 的产品固件。
