# Interoperability

SDK 4 and SDK 3 share wire v3. Both endpoints must use the same wire version. The SDK version and public C ABI identify local builds, and each build uses its matching headers and library.

## Required agreements

Peers must agree on MTU, extension semantics, authentication tag length, trusted connection/epoch and directional security parameters. Only reliable DATA consumes a reliable packet number. Authentication uses an independent 64-bit sequence and a provider-defined algorithm.

## Reproduce the cross-SDK test

In the repository, `test/interop/README.md` identifies the pinned SDK 3 baseline and dependency preparation. Both endpoints use `test/interop/interop_peer.c`. CI links one against the installed SDK 3 and the other against the current installed SDK. The fixture also supports building the current endpoint from source. The driver `test/interop/test_wire_interop.py` passes actual frames over standard streams.

Run from the repository root with a C11 toolchain. Run once for `boot` and once for `full`.

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

Success prints `<profile>: two-way v3 interop passed`. Boot compares HELLO bytes with SDK 3 captures, drops the first ACK in each direction, and checks retransmission and a single delivery. It uses RESET vectors with the SDK 3 CRC to check that repeated RESET rejects sends in the same epoch. Full exchanges HELLO and reordered fragments in both directions with test authentication, then checks SACK and complete reassembly. The driver fails on errors, missing or duplicate deliveries, or a recovery branch that did not execute.

## Evidence

`test/unit/wire/test_wire.cpp`, `test/unit/security/test_security.cpp` and `test/unit/network/test_network.cpp` separately check encoding, authentication inputs, canonical forwarded AAD and routing. The deterministic signing provider checks authentication input binding within the test scope. Products qualify their actual cryptographic provider and board PHY.
