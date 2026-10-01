# Wire layout and resource optimization

## Scope and compatibility

This change simplifies frame sizing, synchronous encoding, borrowed ACK bitmaps, and RX temporary storage. Wire v3, existing public structure ABI, the synchronous PHY lifetime, and external serialization remain unchanged. Private helpers and parameter structures are not SDK interfaces; products continue to use the public API.

No persistent peer or PHY parser state, heap service, or duplicate foundation implementation is introduced. Responsibilities remain divided among wire, datalink, network, transport, and security as described in [Architecture](../protocol/architecture.md).

## Length checks and synchronous encoding

`xgl_frame_measure_lengths` provides the shared checks for payload, TLV extensions, authentication tags, and total frame length. Size helpers return zero for lengths that cannot be represented on the wire. The payload budget clears its output on failure and caps successful results at the 16-bit payload field limit. A sum fitting the host's `size_t` does not establish that the frame is valid.

`xgl_frame_measure` additionally checks borrowed spans and authentication extension conflicts. The regular `xgl_frame_encode_into`, `xgl_frame_serialize`, and authenticated serialization entry points retain their parameter and destination-capacity checks.

Datalink checks the layout, in-place payload offset, and destination capacity within one synchronous send, then continues through private measured entry points. Authentication reuses that layout before signing and finalizing CRC. Plaintext encoding finalizes CRC directly. Both use the same datalink PHY submission boundary.

| Private entry precondition | Requirement |
| --- | --- |
| Layout origin | Successfully measured from this frame and tag length |
| Byte storage | Frame, payload/TLV spans, and layout remain unchanged before encoding continues |
| Destination capacity | Already verified to hold at least `layout.frame_len` bytes |
| Call sequence | One synchronous operation; no cached layout reused for another frame |
| Callbacks | No mutation of borrowed input or instance reentry |

A measured entry continues an internal operation; it must not accept arbitrary network input or a fabricated layout. Necessary wire field encoding checks remain. The entry does not replace association, nonce lifetime, busy, or broadcast policy checks. See [Send API](../guide/send-api.md) for the public contract.

## SACK views and admission limits

`xgl_wire_decode_sack_view` checks the structure and borrows the original TLV bitmap without copying bytes. Failure clears the view. The existing copying helper uses the same decoder and preserves caller outputs until all checks succeed.

Transport no longer reserves a 64-byte temporary bitmap, but still explicitly rejects bitmaps longer than 64 bytes, empty bitmaps, and acknowledgements of future packet numbers. It validates the entire feedback before applying ACKs and retransmissions; oversized feedback cannot remove some reliable records before failing. Wire view encoding capacity and transport admission limits are separate contracts.

The bitmap is borrowed only during synchronous processing and is not retained in a peer or queue. See [Reliability](../protocol/reliability.md) for acknowledgement, capacity, and retry semantics.

## Decode and CRC temporary storage

Fixed-header CRC uses the incremental xgen-crc API for the first 22 bytes followed by two zero bytes, replacing a 24-byte temporary header copy. The algorithm, seed, and byte ranges do not change. The foundation component continues to own the CRC implementation.

Complete-frame decoding writes directly into the output view's header. A common failure exit clears the entire view; payload/TLV spans are exposed only on success. Raw-frame decoding retains fixed-header, TLV boundary, tag-length, consistency, and complete-frame CRC checks. That wire phase left the stream parser's early header check, state, timeout, and resynchronization rules unchanged; later candidate recovery and state compaction are described in [Datalink Optimization](datalink-optimization.md).

No complete view is cached in each link. A private path that skipped the second header CRC at completion was evaluated: its final ELF added 56 B over the CRC simplification under identical conditions, while TLVs still needed decoding at delivery. This experiment was not adopted in production.

## Private structures and source organization

`xgl_frame_params_t` drops the unused `data_type` and `session_epoch` fields. Network still encodes these values in DATA_TYPE and SESSION extensions. The zero-copy construction helper moves into `xgl_frame.c` and continues sharing length and encoding rules.

Merging files does not itself establish a smaller image. This zero-copy helper was already absent from the baseline Boot ELF; removing its separate source file cannot be credited with its source or object size as a saving. Frame construction, layout, and CRC functions remain called by production paths.

## Resource measurement

Comparisons must hold toolchain, profile, feature switches, MTU/RX, peer count, and window capacity constant, and record the final ELF, map, `.su` files, source state, and dependency versions. Workspace is already included in static RAM. A stack reservation is not a measured maximum.

The integrated implementation has completed local final-ELF measurements using Cortex-M0, GCC 15.2.1, `-Os`, one peer, and window 1. Every row reserves an additional 1,024 B of stack:

| MTU/RX | LTO | Flash | Workspace | Static RAM | RAM with reserved stack | Largest single-function stack |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 128 | Off | 15,932 B | 1,136 B | 1,200 B | 2,224 B | 272 B |
| 64 | Off | 15,932 B | 944 B | 1,008 B | 2,032 B | 272 B |
| 128 | On | 12,600 B | 1,136 B | 1,200 B | 2,224 B | 312 B |
| 64 | On | 12,600 B | 944 B | 1,008 B | 2,032 B | 312 B |

Without LTO, Flash decreases by 128 B from the 16,060 B baseline under the same configuration; workspace and static RAM are unchanged. Enabling LTO on this implementation saves 3,332 B relative to disabling it, while increasing the largest single-function stack by 40 B. An earlier source revision's separate LTO experiment measured 12,472 B; the current result is 12,600 B, an increase of 128 B. The earlier experiment must not replace the current result. Every configuration remains above the 8 KiB Flash target.

The local complete matrix is retained in `out/wire-opt-root/final/arm-{128,64}` and the corresponding `-lto` directories. Reports record source state, `lto`, actual optimization flags, and target IPO state. The controlled probe option `XGL_FOOTPRINT_LTO` defaults to off. When enabled, protocol and selected foundation libraries and the final link use LTO. The consumer and layout diagnostic objects remain non-LTO to preserve the generic consumer boundary and inspectable layout. LTO configurations use separate budget files without relaxing default configuration limits.

Isolated RX experiments held all other linked objects constant and produced the following Cortex-M0, GCC 15.2.1, `-Os`, no-LTO, Boot MTU/RX 128 results. These compare local candidates, not the integrated change:

| Variant | Final Flash | Header CRC single-function stack | Complete-frame decode single-function stack |
| --- | ---: | ---: | ---: |
| Baseline | 16,060 B | 32 B | 136 B |
| Incremental CRC | 16,056 B | 16 B | 136 B |
| Incremental CRC and direct header decode | 16,044 B | 16 B | 96 B |

Local evidence is retained under `out/wire-opt-rx/`. Candidate savings cannot be mechanically added to other source changes. LTO changes cross-file inlining and symbol attribution; summing the remaining wire symbols does not measure its post-LTO footprint.

These are locally linked generic probes without target-board execution. Products still need to account for the BSP, PHY/DMA, upgrade logic, Flash write buffers, and interrupt stacks. See [Resource model](../guide/resource-model.md). The largest single-function stack is not a complete call-chain bound.

## Validation and reproduction

Prepare explicitly installed foundation packages, or use `dev/` with five explicit foundation source paths. Tests use fixed GoogleTest 1.16.0; production configuration does not download dependencies. For an already configured build directory:

```sh
cmake --build build/dev-debug --parallel 6
ctest --test-dir build/dev-debug --output-on-failure
python tools/quality.py test --build-dir build/dev-debug
python tools/quality.py format
```

New SACK views and checked-length behavior retain RED/GREEN evidence. The four existing RX contracts passed on the original decoder before the refactor was validated against production code. The focused wire/frame/parser/datalink CTest run passed 152 cases. Local GNU CTest runs passed 630 cases each for Full and Embedded, 79 for Boot, and 114 for Embedded Lite. These are CTest counts for each configuration, not a single GoogleTest count. Other compilers, sanitizers, coverage, and final quality gates have separate records; the listed tests do not imply that those gates passed.

ARM uses the controlled `tools/boot_footprint` project and toolchain. Run from the repository root, replacing example paths with the explicitly prepared repositories:

```sh
cmake -S tools/boot_footprint -B build/wire-arm-128 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/xgen-link/tools/boot_footprint/toolchain-arm-cortex-m0.cmake \
  -DCMAKE_BUILD_TYPE=MinSizeRel \
  -DXGL_DEV_STATUS_SOURCE_DIR=/path/to/xgen-status \
  -DXGL_DEV_BYTES_SOURCE_DIR=/path/to/xgen-bytes \
  -DXGL_DEV_CRC_SOURCE_DIR=/path/to/xgen-crc \
  -DXGL_DEV_MEMORY_SOURCE_DIR=/path/to/xgen-memory \
  -DXGL_DEV_CONTAINERS_SOURCE_DIR=/path/to/xgen-containers \
  -DXGL_FOOTPRINT_FRAME_SIZE=128 -DXGL_FOOTPRINT_RX_SIZE=128
cmake --build build/wire-arm-128 --parallel 6
python tools/boot_footprint/check_budget.py \
  --report build/wire-arm-128/boot-footprint.json \
  --policy tools/boot_footprint/budget-mtu128.json \
  --output build/wire-arm-128/budget-result.json
```

For the 64-byte configuration, use a separate build directory, set both MTU/RX values to 64, and select `budget-mtu64.json`. To measure LTO, repeat the respective configuration in a separate `build/wire-arm-128-lto` or `build/wire-arm-64-lto` directory and add `-DXGL_FOOTPRINT_LTO=ON`. Use `budget-mtu128-lto.json` or `budget-mtu64-lto.json` respectively. A non-LTO report must not be checked against an LTO policy.

See [Release validation](release-validation.md) for the full procedure. Local tests, remote CI, source publication, and board acceptance are reported separately. This page does not claim new remote or hardware results.
