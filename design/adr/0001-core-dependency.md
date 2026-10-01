# ADR 0001：独立公共组件的单向依赖

状态：已采用；本轮正式模块与开发装配分离已通过本地契约、协议回归与 ARM 重跑，具体证据见实施记录。此前五包消费结果保留为历史，不混用两个阶段的来源。远端发布和真实产品验收未执行。

## 决策

公共能力由五个独立 C11 仓库拥有：xgen-status（`xgs_`）、xgen-memory（`xgm_`）、xgen-containers（`xgct_`）、xgen-bytes（`xgb_`）和 xgen-crc（`xgcrc_`）。它们不依赖 xgen-link。协议仓库只维护协议身份、帧、链路、路由、可靠传输、安全及应用 API。

memory 提供 allocator、pool、size_class、arena、tracking、libc_allocator 六项；tiered 的较大档回退及 borrowed/owned 能力收敛到单一 size_class 实现，保持 strict 默认，不保留 tiered target 或包装。containers 提供 list、hash、bitset、ring_buffer 四项；协议仅消费实际需要的能力，ring_buffer 的 DMA 模型不改变 link 的同步 PHY 契约。

协议直接调用所属组件的公开接口，不保留通用功能的 xgl 包装、旧前缀别名或重复实现。`xgl_error_t` 是协议错误域；通用状态使用 `xgs_status_t`，二者在模块边界转换。

## 依赖交付

五包当前独立版本为 0.1.0；消费要求均为 `>=0.1.0,<0.2.0`、ABI 1。正式 CMake 入口先接受父工程已有的完整兼容 targets，否则查找安装包。所有入口校验版本、ABI、target 类型及必需 targets，不在配置中下载，也不搜索基础组件源码目录。

产品在顶层统一选择五包的版本并提供 targets 或安装前缀。link 移除五个 `external` 子模块和生产 `XGL_*_SOURCE_DIR` 路径，旧选项显式报错；不再通过嵌套检出为产品选择第二份依赖。产品使用子模块时，其 gitlink 仍是产品源码版本权威。

独立源码开发由 [dev 装配入口](../../dev/README.md) 负责：五个 `XGL_DEV_*_SOURCE_DIR` 必须显式提供，先装配基础 targets 再接入正式模块。`dev/dependencies.json` 只固定本仓开发/CI 验证输入，不能覆盖产品版本选择。开发依赖获取是显式准备步骤，不能在配置或编译时联网；实际来源、提交和脏状态进入验证记录。

作为产品子目录时，示例、smoke、资源/静态分析及发布辅助默认关闭；开发入口可以显式开启。生产库和必要生成头不依赖开发装配，安装包不导出 dev 路径或测试输入。

此前五子模块和 core 的记录保留为历史，不恢复生产兼容包装。core 退休提交 `b042d51abf45b411a8bf1fdbd98337270f8d024c` 不再包含生产实现；新组件远端是否可获取与本地开发验证分别记录。

安装入口按包复用完整预提供 target 集合，并统一检查兼容版本范围、ABI 和类型。集合不完整时仍解析 package，由提供者执行精确身份校验，避免把不同版本拼成一个包。

完整集合也必须保持包内所有 target 版本一致，不能因各自落在兼容范围内就混用两个补丁版本。共用检查器按包前缀执行该约束，不要求不同独立包使用同一版本号。

静态安装包解析实际所需的 status、bytes、CRC、memory、containers targets，消费者链接 `xgl::xgl`。公开的 `xgm_allocator_t` 使用要求向消费者传播；仅实现需要的依赖按实际链接要求导出。依赖包分别安装，不将其公开头复制进协议安装目录。禁止混用不同 profile 的生成头和库。

## 资源与裁剪

协议 workspace 只规划协议资源类别和容量，分配/释放算法复用 xgen-memory。`xgm_allocator_t` 携带调用者上下文；NULL libc fallback 仅能在允许该行为的 create 边界解析。ACK 标志使用 xgen-containers bitset，初始化失败不能修改原协议状态。

Boot 不包含路由 hash 索引、认证、转发、分片或乱序状态。配置依赖目标不表示所有对象都进入最终 ELF；可选 strings、CRC8、arena、tracking、ring_buffer 不由 link 默认引入。最终体积、堆符号和资源变化以真实链接产物验收。

## 验证

独立组件负责自身行为、包消费、公开接口与质量检查；link 负责协议集成、资源所有权和故障恢复。此前 ACK 行为保留有效 RED/GREEN：RED `b169a10` 发现 128 个标志分配 128 字节及初始化失败改写 allocator 两项缺陷；中间 GREEN `6927196` 对应的工作区通过相同 4/4 用例。该阶段最终 Full 在 GCC/MSVC 各通过 8/8 CTest、510/510 GoogleTest；Embedded 4/4、Boot 2/2 通过。检查点不声称包含原先全部未提交协议重构，本轮依赖入口及默认值变更需重新验收。

迁移记录包含来源提交和脏状态、独立依赖来源、编译器、profile、消费者 ABI、最终 map/ELF。Host 行为、ARM 链接、真实板级 DMA 和远端 CI 分别验收；先前 core 阶段通过结果不充当新五包组合的通过证据。实际状态见 [实施记录](../../REFACTORING_STATUS.md)。

本轮生产契约 10/10 通过，覆盖预提供 targets、安装消费、旧入口拒绝及子目录辅助默认隔离；dev Full 8/8 CTest、Embedded 4/4（均执行 510 个 GoogleTest）、Boot 2/2 通过。显式 dev 源码 ARM 重跑资源与前一阶段一致，未重跑的 MSVC、Linux sanitizer、远端或硬件任务保持独立状态。
