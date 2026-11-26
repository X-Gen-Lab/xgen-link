# 互操作

SDK 4 与 SDK 3 共用 wire v3。对端需使用同一线上版本。SDK 版本和公共 C ABI 属于本地构建身份，由各自的头和库匹配。

## 必须一致的约定

对端须约定 MTU、扩展语义、认证 tag 长度、可信 connection/epoch 和方向性安全参数。只有可靠 DATA 消耗可靠包序号。认证使用独立的 64 位安全序号及 provider 定义的算法。

## 复现新旧端点

仓库中的 `test/interop/README.md` 给出固定的 SDK 3 修订及依赖准备方式。`test/interop/interop_peer.c` 是两个端点共用的 C 夹具，分别链接已安装的 SDK 3 和当前生产库。`test/interop/test_wire_interop.py` 通过标准流转发实际帧。

从仓库根目录执行。独立夹具由 C11 工具链构建。以下命令分别用 `boot` 和 `full` 运行一次。

```sh
profile=boot
work="$PWD/build/wire-interop"
python3 test/interop/build_wire_interop_legacy.py \
  --work-dir "$work" --profile "$profile" --fetch
cmake -S test/interop -B "$work/sdk4-$profile" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DXGL_PROFILE="$profile"
cmake --build "$work/sdk4-$profile" --target xgl_interop_peer
python3 test/interop/test_wire_interop.py \
  "$work/sdk3-$profile/xgl_interop_peer" \
  "$work/sdk4-$profile/xgl_interop_peer" --profile "$profile"
```

成功输出 `<profile>: two-way v3 interop passed`。Boot 还比对 SDK 3 的 HELLO 字节，在两个方向故意丢弃首个 ACK，检查重传和一次应用交付，并用旧版 CRC 制成的 RESET 向量检查重复 RESET 后拒绝同 epoch 发送。Full 在两个方向交换带测试认证的 HELLO 和乱序分片，检查 SACK 和完整重组。脚本遇到错误、漏交付、重复交付或恢复分支未执行时返回失败。

## 证据

`test/unit/wire/test_wire.cpp`、`test/unit/security/test_security.cpp` 和 `test/unit/network/test_network.cpp` 另检查编码、认证输入、规范化转发 AAD 及路由交付。确定性签名 provider 用于检查测试范围内的认证输入绑定。产品负责验证实际密码 provider 和板级 PHY。
