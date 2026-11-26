# Wire v3 新旧 SDK 互通

驱动脚本连接两个独立进程，每个进程通过公开接口链接各自的 XGL 库。Boot 场景比对旧版 HELLO 字节，故意丢弃首个 ACK，检查双向重传、一次交付及重复 RESET 后拒绝同 epoch 发送。Full 场景在两个方向交换带测试认证的 HELLO 和乱序分片，检查 SACK 和重组后的完整交付。

## 准备固定 SDK 3

在当前仓库根目录执行。需要 Git、C11 编译器、CMake 3.24 或以上、Ninja 和 Python 3。旧 SDK 固定为 `f09efca25abe84c344ce8c41d5645006c7b61df9`，五个基础组件固定在该修订的 `dev/dependencies.json` 中。[旧 SDK 装配说明](https://github.com/X-Gen-Lab/xgen-link/blob/f09efca25abe84c344ce8c41d5645006c7b61df9/dev/README.md)

按 `boot` 和 `full` 分别准备。`--fetch` 在浅克隆缺少旧版 Git 对象时也会从 `origin` 获取该修订。

```sh
work="$PWD/build/wire-interop"
profile=boot
python3 test/integration/build_wire_interop_legacy.py \
  --work-dir "$work" --profile "$profile" --fetch
```

`--fetch` 从五个公开仓库各取固定提交。离线已有 Git 对象时，用 `--deps-dir /path/to/checkouts` 代替，目录内为 `xgen-status`、`xgen-bytes`、`xgen-crc`、`xgen-memory` 和 `xgen-containers`。准备脚本从 Git 对象提取源码，在临时目录独立构建并安装 SDK 3，再静态链接旧端点。它打印并校验各提交，发布的测试端点位于 `$work/sdk3-$profile/xgl_interop_peer`。

## 运行双向互通

当前 SDK 测试配置需要显式安装带 GoogleMock 的 GoogleTest 1.16.0。每个 profile 使用独立的构建目录。

```sh
cmake -S . -B "$work/sdk4-$profile" -G Ninja \
  -DXGL_PROFILE="$profile" -DXGL_BUILD_TESTS=ON \
  -DGTest_DIR=/path/to/gtest/lib/cmake/GTest
cmake --build "$work/sdk4-$profile" --target xgl_interop_peer
python3 test/integration/test_wire_interop.py \
  "$work/sdk3-$profile/xgl_interop_peer" \
  "$work/sdk4-$profile/test/xgl_interop_peer" --profile "$profile" \
  --transcript "$work/$profile-transcript.log"
```

Windows 使用对应的 `.exe` 路径。脚本使用确定性的模拟协议时间。每条命令及端点退出默认有 10 秒真实等待上限，可通过 `--timeout-seconds` 调整。转录按处理顺序记录命令、端点回复、丢 ACK 或重排及最终判定，保留失败前的输出。测试认证 provider 用于核对 nonce、AAD 和负载绑定，产品应使用自己的认证实现。
