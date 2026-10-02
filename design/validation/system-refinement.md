# 可选有界运行、scope 与交付完善验证

日期：2026-10-02。实现决策见 [ADR 0013](../adr/0013-optional-bounded-runtime.md)，使用方式见[扩展选项](../../docs/zh/guide/advanced-options.md)及[有界运行](../../docs/zh/guide/bounded-runtime.md)。本记录只报告本地执行；未推送，也不代表远端 CI 或真实硬件验收。

## 实现和兼容范围

`48d2198` 固定本轮生产逻辑，`f16ffeb` 将指针数组清零的 `void*` 转换显式写出以通过 clang-tidy；`2e14f47` 补充时间和配额参数顺序的局部诊断说明，是最终编译输入冻结点。这两步均未改变十个 ARM ELF 的已分配文件段。保留 C11、wire v3 和 `xgl_config_t` 的现有 ABI，所有 profile 的新能力默认 OFF。版本化 options 控制消息槽、绝对期限和 scoped 回调；新增执行预算、当前资源快照及显式安装 scope 的双向认证策略。可靠全量清理改为一次清桶再线性释放。

Parser 复用原 cache 保存未处理输入，不新增第二份 MTU 缓冲。候选计数、绝对时间和错误恢复位置可跨步续跑；只有实际调用 PHY 才更新轮询时间。Transport 只把实际维护提交计入配额，轮换 peer 起点；同轮终止或释放的工作不残留 pending。可靠忙期和重组截止在接纳前检查，晚 ACK/最终片段不能消除已经到期的事实。

旧入口与默认配置继续可用；ADVANCED_RUNTIME 改变私有布局和构建身份，消费者使用同一生成头。协议导出不增加 GoogleTest、质量工具或平台库依赖。消息槽缩减、BUSY、终止和释放保持显式资源所有权。

## TDD 与复审

| 范围 | 真实 RED | GREEN / 证据 |
| --- | --- | --- |
| 空测试及主机入口 | `ce3d99d`：默认空 CTest 成功、缺受限主机测试预设 | `9c7999b`、`691ad8d`；`delivery/final-presets.log` |
| 版本化 options | `9ab47d8`：缺类型与入口编译失败 | `48d2198`；`api/advanced-green.xml`，最终各 profile 回归 |
| 有界 transport | `4ce4e03`：缺维护配额和期限契约 | `transport/final-tests.xml`，266 项阶段回归 |
| 严格重组截止 | `4b8c4ec`：到期最终片段仍被交付 | `transport/absolute-red-tests.log` 与最终 transport 结果 |
| 释放后 pending | `26e2822`：已清空 scope 仍要求续跑 | `transport/pending-red-tests.log` 与最终结果 |
| Parser 续跑 | `0699ae1`：缺候选预算入口 | `parser/green.xml`；性质种子 5785420 |
| 真实 PHY 轮询时间 | `0dde6eb`：暂停时吃掉轮询间隔 | `parser/poll-red.xml` 与后续 GREEN |
| 已安装安全 scope | `1002950`：明文误接纳、反向未认证及布局不一致 | `24e7185`、`48d2198`；`security/final.xml` 53 项 |
| 构建选项与身份 | `32b83fc`：非法 advanced 值被当成 true | `api/build-options-green.log`，3 项契约 |
| 类型头独立使用 | `b583478`：初始化宏展开缺配置定义 | `api/header-green.log`，C11/C++17 首包含 |
| 扩展 ELF 身份 | `f62184d`：缺编译能力和实际调用路径标识 | `5600782`；预算 8 项、布局 6 项、实际 ARM 2 项 |

表内证据路径均相对 `out/system-refinement/`，是本地运行产物，不作为已经发布的远端制品。正常主机/受限 profile、安装消费者、sanitizer 和硬件结果分别计数。

预验保留的失败还包括：严格 C++17 不允许把预处理指令嵌在 GoogleTest 宏实参中；Boot 新测试误以为一次可读取 68 B，而其编译 chunk 为 32 B。修正测试写法和实际容量假设，不删除历史回归、不放宽生产限制。关于未启用重组容量相加溢出的怀疑，经真实字段宽度和用例验证被否证，没有据此修改生产代码或虚构 RED。

## 最终 Cortex-M0 资源

固定 ARM GNU 15.2.1，通用 Cortex-M0 链接布局，MinSizeRel；普通和 LTO 分别构建。五个依赖显式提供，所有报告的生产源码摘要为 `85d39a3f46a38a3849bde77979743f12604f3e468b64432f9023b68199c3543f`。汇总为 `arm/final-summary.json` 和 `arm/final-source-identity.json`；显式转换前后 ELF 对照为 `arm/cast-only-elf-comparison.json`，最终注释变化的对照为 `arm/comment-only-elf-comparison.json`。旧阶段报告单独保留。

| 组合 | Flash 普通 / LTO | workspace MTU128 / 64 | RAM 含预留栈 MTU128 / 64 |
| --- | ---: | ---: | ---: |
| default，扩展 OFF | 16,060 / 12,752 B | 1,072 / 880 B | 2,160 / 1,968 B |
| small，扩展 OFF | 15,828 / 12,580 B | 1,040 / 848 B | 2,128 / 1,936 B |

八组原预算全部通过，未修改限额或历史基线。相比前一阶段默认普通/LTO 减少 40/28 B，small 减少 40/32 B；预留 RAM 不变。默认 LTO 距 12,800 B 门槛还有 48 B。

| 扩展 Boot，MTU128，LTO | Flash | workspace | 静态 RAM | RAM 含预留栈 |
| --- | ---: | ---: | ---: | ---: |
| 宏 ON，旧入口探针 | 13,988 B | 1,128 B | 1,192 B | 2,216 B |
| 宏 ON，实际扩展 API 探针 | 14,424 B | 1,128 B | 1,200 B | 2,224 B |

后一探针调用 options 测量/静态创建、bounded step 和资源快照，但并不覆盖所有策略组合，也不包括 Boot 裁掉的认证与分片。它们没有套用默认预算。十份 ELF 均通过禁堆和未解析符号检查。

所有 RAM 总量包括 **1,024 B 栈预留**。`.su` 最大单函数栈普通/LTO 为 192/208 B，扩展为 216 B；它不是完整调用链、ISR 嵌套或板上高水位。资源优化未达到 8 KiB Flash 目标，不能把本探针当作可直接烧录的完整 Bootloader。

## 行为、质量和来源

Windows GNU 的四 profile × advanced ON/OFF 八组均通过。下面将协议与辅助测试分开列出，辅助包括实际安装后的纯 C11 消费者；Full 保留全部 510 个历史协议名称。

| Profile | OFF 协议 | ON 协议 | 每种模式辅助 | OFF / ON 合计 |
| --- | ---: | ---: | ---: | ---: |
| Full | 813 | 854 | 7 | 820 / 861 |
| Boot | 220 | 242 | 2 | 222 / 244 |
| Embedded Lite | 311 | 345 | 6 | 317 / 351 |
| Embedded | 813 | 854 | 7 | 820 / 861 |

MSVC 19.40.33811.0 的 Full advanced ON 为 **861/861**，其中 854 个协议用例与 GNU 同名，另有 7 项辅助；无重复、跳过或漏执行。最终前后六仓编译/测试输入摘要一致，证据为 `windows-final/summary.json`、`input-before.json`、`input-after.json`；失败阶段保留在 `windows-final/attempt-history.json`。

MSVC 旧增量目录曾有 87 项失败：本机只有中文编译器资源，CP936 与 CMake 的 `/showIncludes` 前缀识别不一致，Ninja 没有记录头文件依赖，复用了旧对象。新开发者环境执行 `chcp 65001` 后重新配置，确认真实头文件依赖及生成头时间戳触发重编译，再通过完整回归。仅设置 `VSLANG=1033` 不能补足本机缺失的资源。此处没有通过修改生产行为绕过失败。

Linux GNU Full advanced ON **861/861**；ASan/UBSan **860/860**，开启泄漏检测和错误即停。sanitizer 配置只排除安装消费者 smoke，该消费者由普通 Full 独立执行；没有将跳过的用例计为通过。清单、JUnit、54 项 property 标签及种子 `5785420` 已核对，无失败、重复或跳过，历史名称均保留。

生产覆盖率为行 **4,563/4,859（93.908%）**、函数 **342/342（100%）**、分支 **3,031/3,610（83.961%）**。Clang-Tidy 19.1.0 完整检查 43 个生产翻译单元通过。冻结后的构建前后与所有检查结束时，编译/测试输入清单 SHA-256 均为 `ff955f5714b22a461f8789206746c7fa310938fad6eea5c83601adcc19aadb08`。该清单范围与 ARM 的生产源码摘要不同，不应直接比较摘要值。

Linux 证据统一位于 `linux/final-4/verification-summary.json` 和该目录的 `evidence/`。报告中的 dirty 状态如实反映当时仍在收尾的文档及 CI 文件；编译输入已单独核对。此前 clang-tidy 对指针数组清零和同类型参数顺序的失败均保留，最终运行通过整个范围，不复用部分预检代替全量结果。

Cppcheck 2.21.0 的 43 个生产单元在 `f16ffeb` 通过，此后源码只有 `2e14f47` 的四行局部签名说明。固定格式、文本、全仓 pre-commit、严格 Doxygen、双语 QA 和严格 MkDocs 均通过；质量 JSON 汇总保留于 `out/system-refinement/final-quality/`。各工具失败、预验与最终通过证据分别保留，不使用旧二进制或历史覆盖率替代当前源码验证。

## 产品验收边界

预算限制候选或提交次数，不限制一次 CRC、回调、RX 即时 ACK、memmove 或扫描的 CPU 周期。产品仍需测量完整 step 的最坏执行时间，并实现 DMA ring、cache 一致性、溢出处理、无丢唤醒的 owner 任务以及容量恢复通知。

TX drained 只表示协议确认；真实 Flash 落盘、电机动作或业务事务完成需要应用确认。安全会话关闭记录不等于可无限复用的槽，密钥更新、nonce 域和重启新鲜性仍由产品管理。本地验证与远端 CI、源码发布及板级验收保持区分。
