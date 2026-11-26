# 跑通首次收发

## 构建并运行

使用 CMake 3.24 或以上、Ninja 和 C11 编译器，从仓库根目录构建主机 Boot 示例。

```sh
cmake -S . -B build/first-link -G Ninja -DXGL_PROFILE=boot \
  -DXGL_BUILD_EXAMPLES=ON -DXGL_BUILD_NOHEAP_SMOKE=ON
cmake --build build/first-link
ctest --test-dir build/first-link --output-on-failure --no-tests=error -R '^xgl_echo_example$'
```

CTest 应运行并通过一个 echo 用例。失败时检查 `build/first-link/Testing/Temporary/LastTest.log`。本例验证同步模拟 PHY 下的双端交付。

## 存储与生命周期

通过 `xgl_config_get_preset_boot()` 配置单 peer、单可靠槽及 128 字节 MTU，再设置本地节点、路由、PHY 和应用回调。配置及引用对象保留到销毁。

调用 `xgl_memory_requirements()`，检查精确大小和对齐，为 `xgl_init_static()` 提供足够容量。不同 profile 和容量需要不同空间。也可由 `xgl_create()` 通过选定后端预留相同布局，然后调用 `xgl_init()`。

## 主循环

```c
const xgl_work_budget_t budget = {128U, 1000U};
(void)xgl_step(handle, now_ms, &budget);
uint32_t delay_ms;
if (xgl_next_timeout(handle, now_ms, &delay_ms)) {
    /* 应用决定下次唤醒时机。 */
}
```

在回调之外使用 `xgl_send_at()`。Flash 或应用存储忙时，接收接纳回调可返回 BUSY。PHY TX 返回前必须完成读取或复制。

## 可运行程序

[示例](examples.md)提供生命周期完整的程序。Boot 升级演示模拟主机节流、固定块、慢 Flash 和丢 ACK，不是安全硬件 Bootloader。
