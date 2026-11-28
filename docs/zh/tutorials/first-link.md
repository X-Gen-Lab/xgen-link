---
id: tutorials/first-link
type: tutorial
audience: user
source_language: zh_CN
---

# 跑通首次收发

## 构建并运行

使用 CMake 3.24 或以上、Ninja 和 C11 编译器，从仓库根目录构建双端 echo 示例。

```sh
cmake -S . -B build/first-link -G Ninja -DXGL_PROFILE=embedded-lite \
  -DXGL_BUILD_EXAMPLES=ON
cmake --build build/first-link --target echo_server
./build/first-link/examples/echo_server/echo_server
```

程序应输出 `Deferred echo: PASS`。本例验证同步模拟 PHY 下的双端交付。Windows 的可执行文件带 `.exe` 后缀。

## 存储与生命周期

示例从 Tiny preset 开始，将可靠窗口与 peer 容量各设为 1，使用 128 字节 MTU。为两端设置各自的本地节点、到对端的路由、PHY 和应用回调。配置及引用对象保留到销毁。

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

在回调之外使用 `xgl_send_at()`。应用存储忙时，接收接纳回调可返回 BUSY。PHY TX 返回前必须完成整帧读取或复制。产品处理 `xgl_step()` 返回的接收或维护错误，详见[发送 API](../guides/send.md)。

## 可运行程序

[示例](examples.md)提供生命周期完整的程序，包含可靠应用块和多跳收发。
