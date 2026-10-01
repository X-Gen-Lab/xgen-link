# 协议结构优化验证记录

日期：2026-10-01。起点：`b158d60066f3ab6af8f8ac29101ee50d8cb8826f`。实施分支：`feat/independent-memory-containers`。设计决策见 [ADR 0005](../adr/0005-protocol-structure.md)。

## 已实施范围

- 生产 C 文件从 63 个收敛到 45 个，其中 transport 从 34 个收敛到 17 个；按状态所有者组织 TX、RX、ACK、peer、runtime。
- 纯 wire 帧规划/编码、认证封装和唯一 datalink PHY 提交分离；普通、认证、原地及转发路径共享规则。
- parser 属于 datalink，每唯一 PHY 预留一份 parser/cache。完整 CRC 只验证一次，错误分类经栈上输出传递；security context 归实例所有。
- 可靠接纳直接返回记录，ACK/SACK 先完整校验并提交确认再快重传，重组完成消息保留字节预算至统一 release。
- 私有头归属 `src/<layer>`，公开安装内容不变；测试按层归档，CMake 源码、SDK 安装和开发辅助分别组织。
- Full/Embedded 补齐 RTT 观察和完整 workspace 预留统计；Boot 编译关闭统计存储/计数及详细错误文字，保留错误码、回调和明确的 unsupported 行为。

wire v3 与公开结构 ABI 未改变。私有 API 允许随实施调整，不作为产品消费入口。五个基础组件及 quality 的固定输入沿用开发清单，未新增生产依赖副本。

## TDD 和缺陷证据

每项行为先执行失败用例，再改生产实现；目录归并不伪装成新的行为 RED。日志位于忽略的 `out/refactor-*`，测试源码和以下检查点可重放。

| 行为 | RED | GREEN |
| --- | --- | --- |
| SACK 的 BUSY 不阻断后续确认；非法发送不创建 peer | `bcaa5c9` | `1072314` |
| 完整帧之前拒绝取出；跨度/长度拒绝 | `ce0a434` | `1d0cb37` |
| 转发通过统一出口并拒绝缺失 PHY | `2d47362` | `85188dd` |
| 共享 PHY 的多路由只预留一个 link | `ee42145` | `1e3a890` |
| 重组完成后仍保留预算至消息 release | `b6c835d` | `352ea81` |
| 纯帧规划、parser 借用视图与 ACK view | `bb1a3ce` | `2e299e1` |
| 可靠接纳返回记录而非二次索引 | `076be8c` | `df1346d` |
| 真实双端 ACK 的 RTT 与预留量统计 | `016fbb2` | `34a37a5` |
| 认证和普通序列化拒绝相同非法跨度 | `0c16399` | `6b26749` |
| Boot 统计 API 明确不支持且不改写输出 | `d61b0a3` | `7b87af4` |
| 原地容量先于指针计算、借用 RX 帧长限制 | `aa40fe9` | `7b87af4` |
| Boot 回调保留错误码但裁掉文字 | `0667f07` | `2a78e22` |
| 流式/原始帧 CRC 分类与回调一致 | `ac3dc9c` | `70e2d17` |

严格 MSVC Boot 编译另有真实 RED：常量统计条件、未使用参数和不可达返回。修复使用编译分支与显式 unused，不关闭告警。扩大私有头分析范围后，Clang-Tidy 唯一新增诊断是容量与偏移的相邻整数参数；仅函数签名登记既有局部例外，并补全参数文档，未抑制函数体。

## 本地验收

| 验证 | 结果 | 证据目录/文件 |
| --- | --- | --- |
| Windows GNU 13.2 Full / Embedded | 各 582/582 CTest；各 6/6 runner 契约 | `out/refactor-root/{full,embedded}-*.log`、`native-matrix.json` |
| Windows GNU Boot | 2/2，含静态生命周期和安装消费 | `out/refactor-root/boot-2.log` |
| MSVC 19.40 Full | 582/582；6/6 runner 契约 | `out/refactor-root/msvc-{build,test,contracts}.log` |
| MSVC Boot MinSizeRel | `/W4 /WX` 编译与静态 smoke 成功 | `out/refactor-root/msvc-boot-*.log` |
| Linux GNU 11.4 Full | 582/582，固定 shared test runner | `out/refactor-linux/evidence/full-test` |
| Linux ASan / UBSan | 581/581，含泄漏检查和错误立即退出 | `out/refactor-linux/evidence/asan-test` |
| 头与辅助构建 | 29 个公开/私有/生成头各以 C11/C++17 首包含；3 个 benchmark 编译 | `out/refactor-wire/build-layout-consumers.log` |
| 依赖 / 开发准备 / 发现策略 | 11/11、11/11、3/3 | `out/refactor-contracts/run-hs7uji8e/result.json`、`out/refactor-root/{dev,discovery}-tests.log` |
| Cppcheck 2.21 / Doxygen 1.16 | 通过，45 个生产 C 文件/4 个公开头 | `out/refactor-root/quality-{cppcheck,docs}-final.json` |
| Clang-Tidy 19.1 | 全 45 个生产翻译单元通过，包含私有头诊断 | `out/refactor-linux/evidence/full-tidy` |
| text / format / pre-commit / 双语文档 | 通过固定 19.1.5 格式工具和文档 QA | `out/refactor-root/pre-commit.log`、`out/reports` |
| 严格文档站点 | MkDocs 构建成功 | `out/refactor-root/mkdocs.log`、`out/refactor-docs-site` |

582 项由 575 个 GoogleTest 和 7 个 smoke/示例构成。原 510 个历史名称逐项保留，属性测试继续记录可回放种子。Full/Embedded/Boot 各使用独立生成配置和构建目录，安装消费者实际使用导出包。

ASan 使用 `detect_leaks=1:halt_on_error=1`，UBSan 开启立即退出和栈回溯。其 581 项与 Full 的唯一清单差异是独立安装消费者：该消费者由普通 Full 构建验证，避免 sanitizer 构建中启动不带相同插桩的嵌套消费者。未发现越界、未定义行为或泄漏报告。

Linux 覆盖率：行 **3665/3988（91.9%）**、函数 **269/270（99.6%）**、分支 **2308/2842（81.2%）**，分别满足 80% 门槛。详细报告包含全部 45 个生产 C 文件和 4 个含可执行 inline 的私有头，没有为通过门禁移除文件或排除分支。工具、源码 SHA 和原始报告保存在 `out/refactor-linux/evidence/full-coverage`。

## Cortex-M0 资源

使用同一 GCC 15.2.1、`-Os`、无 LTO、单 peer、window 1、MTU 128 和显式五依赖，分别重跑起点与优化后源码。完整消费 ELF 包含启动和实际 C 库依赖；报告检查无堆服务与未解析符号。

| 项目 | 起点重跑 | 优化后 |
| --- | ---: | ---: |
| Flash | 16,764 B | 15,692 B |
| workspace | 1,424 B | 1,168 B |
| 静态 RAM | 1,488 B | 1,232 B |
| 预留栈 | 1,024 B | 1,024 B |
| RAM 合计 | 2,512 B | 2,256 B |
| 最大已链接单函数栈 | 392 B | 392 B |

证据为 `out/refactor-arm-baseline/boot-footprint.json` 与 `out/refactor-arm-final/boot-footprint.json`，同时保留 ELF、map、布局与 `.su`。布局探针只包含私有类型头，不再编译另一份生产 `.c`。重构工作区的实际提交和未提交状态按报告保存，不把它描述成已经发布的干净 tag。

满足本测量工程的 64 KiB Flash / 8 KiB RAM 限额；尚不满足 8 KiB Flash / 1 KiB workspace 目标。1 KiB 栈是预留量，392 B 是单函数值，都不是完整调用链或 ISR 的板上高水位。真实板卡、时延、Flash 断电和生产密码 provider 仍需产品验收。

## 交付边界

本记录描述本地实际命令、输入和结果。用户已有 `.spec-workflow/` 未纳入提交。保留功能分支，不合并 main 或创建发布 tag；远端验证应检查本次推送提交对应的 Actions，不能用前一阶段的绿色运行替代。
