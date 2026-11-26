# Wire v3 新旧 SDK 互通

驱动脚本连接两个独立进程，每个进程通过公开接口链接各自的 XGL 库。Boot 场景比对旧版 HELLO 字节，故意丢弃首个 ACK，检查双向重传、一次交付及重复 RESET 后拒绝同 epoch 发送。Full 场景在两个方向交换带测试认证的 HELLO 和乱序分片，检查 SACK 和重组后的完整交付。

1. 按 [SDK 3 开发入口](https://github.com/X-Gen-Lab/xgen-link/blob/f09efca25abe84c344ce8c41d5645006c7b61df9/dev/README.md)在独立目录准备固定旧版源码及依赖。分别配置 `XGL_PROFILE=boot` 和 `XGL_PROFILE=full`，构建并安装到两个独立前缀。下面的 `old_sdk` 指向待测 profile 对应的安装前缀。
2. 在当前仓库根目录设置 `profile` 和 `old_sdk`，然后分别构建两个 peer。新版端点需要 CMake 能发现 GoogleTest 1.16.0，测试驱动使用 Python 3。

```sh
profile=boot # 对 full 重复以下步骤，并选择对应的旧 SDK 前缀。
old_sdk=/absolute/path/to/sdk3-boot
work="$PWD/build/wire-interop"
mkdir -p "$work/legacy-source"
cp test/integration/interop_peer.c "$work/legacy-source/"
cat > "$work/legacy-source/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.24)
project(xgl_interop_peer LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
find_package(xgl 3 CONFIG REQUIRED)
add_executable(xgl_interop_peer interop_peer.c)
target_link_libraries(xgl_interop_peer PRIVATE xgl::xgl)
CMAKE
cmake -S "$work/legacy-source" -B "$work/sdk3-$profile" -G Ninja \
  -DCMAKE_PREFIX_PATH="$old_sdk"
cmake --build "$work/sdk3-$profile"
cmake -S . -B "$work/sdk4-$profile" -G Ninja \
  -DXGL_PROFILE="$profile" -DXGL_BUILD_TESTS=ON
cmake --build "$work/sdk4-$profile" --target xgl_interop_peer
python3 test/integration/test_wire_interop.py \
  "$work/sdk3-$profile/xgl_interop_peer" \
  "$work/sdk4-$profile/test/xgl_interop_peer" --profile "$profile"
```

Windows 使用对应的 `.exe` 路径。测试认证 provider 用于核对 nonce、AAD 和负载绑定，产品应使用自己的认证实现。
