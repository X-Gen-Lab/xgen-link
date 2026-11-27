# Wire v1 双向互通

脚本连接两个独立进程，通过模拟字节流交换各自生产库生成的报文。

- Boot 双向丢弃首个 ACK，检查重传、一次交付和重复 RESET 后拒绝同 epoch 发送。另将一个 DATA 改为 Wire v3 版本字节并重新计算两处 CRC，检查 Wire v1 端点拒收且不反馈。
- Full 双向独立提交两条认证 DATA，重排完整帧，检查 SACK 和按序各交付一次。测试认证 provider 校验 nonce、AAD 和负载绑定，产品提供实际认证实现。

## 使用安装包运行

在仓库根目录执行。需要 C11 编译器、CMake 3.24 或以上、Ninja 和 Python 3。分别使用 `boot` 和 `full`。示例安装当前 SDK 5，随后在两个独立构建目录中按精确安装版本链接端点。

```sh
profile=boot
work="$PWD/build/wire-interop-$profile"
cmake -S . -B "$work/library" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DXGL_PROFILE="$profile"
cmake --build "$work/library"
cmake --install "$work/library" --prefix "$work/sdk"
version=$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$work/library/CMakeCache.txt")

for endpoint in a b; do
  cmake -S test/interop -B "$work/$endpoint" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DXGL_PROFILE="$profile" \
    -DXGL_INTEROP_SDK_PREFIX="$work/sdk" -DXGL_INTEROP_SDK_VERSION="$version"
  cmake --build "$work/$endpoint" --target xgl_interop_peer
done

python3 test/interop/test_wire_interop.py \
  "$work/a/xgl_interop_peer" "$work/b/xgl_interop_peer" --profile "$profile" \
  --transcript "$work/exchange.log"
```

在新的端点构建目录中同时省略两个 `XGL_INTEROP_SDK_*` 选项，即可从当前源码构建。Windows 使用可执行文件的 `.exe` 路径。

每条命令及端点退出默认有 10 秒真实等待上限，可用 `--timeout-seconds` 调整。`--transcript` 记录命令、回复、模拟链路变化和最终判定。历史 Wire v3 回归保存在 [旧版测试记录](https://github.com/X-Gen-Lab/xgen-link/tree/d775c8a2fbc2dabcfc2406034e5cdc43808954bd/test/interop)。
