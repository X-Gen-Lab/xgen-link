# API 契约完善验证

## 范围与检查点

基线为 `58ab3b9`，工作分支为 `feat/independent-memory-containers`。本轮保留已有未跟踪的 `.spec-workflow/`，不推送、合并或创建版本 tag。

| 阶段 | 检查点与实际证据 |
| --- | --- |
| 发送与配置 RED | `a4fd837`：发送 15 项中 10 项失败；配置 7 项中 6 项失败，CTest 退出 8。新增创建错误与预算查询 API 取得预期缺声明编译 RED，退出 1 |
| 资源定制 API RED | `39244f0`：Full/RTO OFF 的 RTT 历史断言失败；window 1 的 4 项中 3 项失败。仅缺文件或私有测试编译失败的记录不计入该运行 RED |
| 小路由兼容 RED | `7e01834`：全局 128 B、认证 tag 32 B、64/128 B 混合路由被错误的逐路由 ACK 门槛拒绝 |
| 主实现 GREEN | `56b4020`：API/配置/发送/清理 43/43；相关发送等回归 351/351；Full 默认协议 800/800、固定 RTO 协议 804/804；四组公开 API 各 49/49 |

生产代码采用 C11，GoogleTest 1.16.0 使用严格 C++17。CTest 保留实际名称和 `unit;xgl` 标签；性质测试默认回放种子为 `5785420`。公开配置及请求结构体布局没有变化，也没有新增实例状态。

本地证据位于 `out/api-refinement/`：`send/`、`config/`、`matrix/` 和根 `root-api-final.xml`。四组公开 API 为 Full AUTO/RTO ON、Full AUTO/RTO OFF、Full window 1/RTO OFF、Embedded window 1/RTO OFF；这是明确选择的 49 项 API 集合，不代替所有资源组合的整库验证。

## 资源门槛补充 RED

固定 GCC 15.2.1、Cortex-M0、Boot window 1、RTO OFF、RX chunk 32，MTU/RX 128 和 64 各自构建最终 ELF。`56b4020` 的普通模式为 Flash 16,212 B，两个普通 policy 通过；LTO 为 12,844 B，超过既有 12,800 B 门槛 44 B，两个 LTO policy 退出 1。门槛保持不变，继续减少新增检查的代码开销。

对应报告为 `out/api-refinement/arm-final-small-{128,64}-{off,on}/boot-footprint.json` 和 `budget-result.json`。工作区分别为 1,040/848 B；静态 RAM 分别为 1,104/912 B，加 1,024 B 栈预留后为 2,128/1,936 B。单函数栈普通/LTO 为 192/208 B。栈预留不是实测调用链上界，探针不代表板级 Boot 验收。

安装包 fixed-RTO 的四项测试标签也取得有效 RED：发现结果只包含 `xgl`，缺少 `unit;transport;fixed-rto`。新断言和发现 JSON 在 `out/api-refinement/matrix/fixed-rto-labels-red/`，真实安装消费和编译已执行；下一步修复标签转义后重跑原资源配置契约。

## 验证边界

本页逐项记录本地结果。跨编译器、sanitizer、覆盖率、静态检查和最终资源复验完成后补充；没有远端 CI、真实 PHY/DMA、密码 provider 或硬件运行结果。
