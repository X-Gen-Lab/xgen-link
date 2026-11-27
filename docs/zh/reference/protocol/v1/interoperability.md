# 互操作

端点及中继使用 Wire v1。SDK 版本、配置 ABI 和资源身份用于本地构建，每个端点使用自身安装包或源码构建中的配套头和库。

## 必须一致的约定

对端须约定 MTU、扩展语义、认证 tag 长度、可信 connection/epoch 和方向性安全参数。只有可靠 DATA 消耗可靠包序号。认证使用独立的 64 位安全序号及 provider 定义的算法。

## 复现 v1 端点互通

`test/interop/interop_peer.c` 是两个端点使用的公开 API 夹具。`test/interop/test_wire_interop.py` 启动两个独立进程，通过标准流交换实际帧。

从仓库根目录执行。独立夹具由 C11 工具链构建。分别用 `boot` 和 `full` 运行下列命令。

```sh
profile=boot
work="$PWD/build/wire-v1-interop"
for endpoint in a b; do
  cmake -S test/interop -B "$work/$profile-$endpoint" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DXGL_PROFILE="$profile"
  cmake --build "$work/$profile-$endpoint" --target xgl_interop_peer
done
python3 test/interop/test_wire_interop.py \
  "$work/$profile-a/xgl_interop_peer" \
  "$work/$profile-b/xgl_interop_peer" --profile "$profile"
```

可在新的端点构建目录使用安装包，传入 `-DXGL_INTEROP_SDK_PREFIX=/path/to/sdk` 和 `-DXGL_INTEROP_SDK_VERSION=5.0.0`。端点要求该前缀中的 SDK 版本和 profile 匹配。

Boot 场景检查双向丢 ACK 后重传、应用只交付一次，以及重复 RESET 后拒绝同 epoch 发送。Full 场景交换带测试认证的 DATA，调换两个独立 DATA 帧的到达顺序，检查 SACK 和按序交付。脚本在错误、漏交付、重复交付或指定恢复分支未执行时返回失败。

## 证据

`test/unit/wire/test_wire.cpp`、`test/unit/security/test_security.cpp` 和 `test/unit/network/test_network.cpp` 检查编码、认证输入、规范化转发 AAD 及路由交付。确定性签名 provider 用于检查测试范围内的认证输入绑定。产品负责验证实际密码 provider 和板级 PHY。
