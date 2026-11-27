# Interoperability

Endpoints and relays on a path must use the Wire v1 encoding specified here. Replace endpoints and relays that use the earlier Wire v1 encoding together during deployment. The version byte is `1` in both revisions, so ensure all devices use the same flags and TLV assignments. Each build uses its matching headers and library.

## Required agreements

Peers must agree on the path MTU, extension semantics and authentication policy. With authentication, also agree on the tag length, trusted connection/epoch and directional security parameters. Only reliable DATA consumes a reliable packet number. Authentication uses an independent 64-bit sequence and a provider-defined algorithm.

## Reproduce the endpoint test

Run from the repository root with a C11 toolchain, CMake, Ninja and Python 3. Repeat with `profile=full` to exercise authenticated traffic.

```sh
profile=boot
work="$PWD/build/wire-v1-$profile"
for endpoint in a b; do
  cmake -S test/interop -B "$work/$endpoint" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DXGL_PROFILE="$profile"
  cmake --build "$work/$endpoint" --target xgl_interop_peer
done
python3 test/interop/test_wire_interop.py \
  "$work/a/xgl_interop_peer" "$work/b/xgl_interop_peer" --profile "$profile"
```

Boot drops the first ACK in each direction and checks retransmission and single delivery. It also checks repeated RESET and rejection of a frame carrying a different version. Full reorders two authenticated DATA frames and checks SACK and single, ordered delivery. A successful run prints `<profile>: two-way Wire v1 interop passed`. CI builds each endpoint independently against the installed SDK. See [fixture instructions](https://github.com/X-Gen-Lab/xgen-link/tree/main/test/interop) for installation options and transcript handling.

## Evidence

`test/unit/wire/test_wire.cpp`, `test/unit/security/test_security.cpp` and `test/unit/network/test_network.cpp` separately check encoding, authentication inputs, canonical forwarded AAD and routing. The deterministic test signing provider checks authentication input binding. Products qualify their actual cryptographic provider and board PHY.
