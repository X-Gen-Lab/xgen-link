# Datalink Receive and Resource Optimization

This work starts from `d7bbb75` and improves receive recovery, stack use and private interfaces. Wire v3, public structure ABI, profile IDs and the five independent foundation dependencies remain unchanged. Measurements use a fixed Cortex-M0 probe, not a complete product or hardware qualification.

## Implemented Changes

| Area | Result |
| --- | --- |
| Candidate deadline | Replacing overlapping magic refreshes its start; later header/payload progress never extends the deadline |
| Oversized frames | Reject impossible minimum lengths at the fixed header, then check authentication trailer space after extensions |
| Parser hot path | Require a view; the legacy byte adapter supplies its own temporary view |
| Parser state | Remove the duplicate CRC index, use an 8-bit header length, retain actual cache progress and time |
| Receive structure | Remove the metadata wrapper, share wire status classification, delegate polling input to synchronous span consumption |
| Layer boundaries | Separate packet/frame TX and RX endpoints, statically bound by the instance |
| Read chunk | Select its compiled capacity and record it in installed headers and resource reports |

The span entry still advances the parser byte by byte. It consolidates framing, synchronous delivery and recovery; it does not imply asynchronous DMA ownership, faster bulk parsing or zero-copy reception. Complete views borrow the cache only during synchronous delivery. BUSY still consumes the rest of the input and returns the first delivery error.

## Complete Cortex-M0 Image

Arm GNU 15.2.rel1 / GCC 15.2.1, `-Os`, Boot, one peer, route and link, window one. RX cache equals MTU. The additional stack reservation remains 1,024 B.

| MTU/RX | LTO | Current chunk | Flash | Workspace | Static RAM | RAM including reserved stack | Largest function stack |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 128 | Off | 32 B | 15,988 B | 1,112 B | 1,176 B | 2,200 B | 192 B |
| 64 | Off | 32 B | 15,988 B | 920 B | 984 B | 2,008 B | 192 B |
| 128 | On | 32 B | 12,600 B | 1,112 B | 1,176 B | 2,200 B | 224 B |
| 64 | On | 32 B | 12,600 B | 920 B | 984 B | 2,008 B | 224 B |

The previous chunk was 128 B: ordinary Flash was 15,932 B and largest function stack 272 B; LTO was 12,600 B and 312 B. Ordinary Flash **increases by 56 B**; LTO is unchanged. Workspace, static RAM and total RAM each decrease by **24 B** at either MTU. Stack changes combine strict views, span separation and the smaller chunk, not just algorithm changes.

On the 32-bit target the parser changes **32→24 B**, each PHY link **48→40 B**, and the four endpoints together **48→32 B**. This explains the 24 B persistent saving with one link. The old metadata wrapper was absent from this Boot ELF, so deleting it contributes no Boot Flash saving. Linked ordinary datalink symbols actually increase from 1,290 B to 1,366 B; changes in other layers and alignment also contribute to the complete image above.

## Interpreting Stack Measurements

Without LTO, `feed_byte_view` function stack changes **104→40 B**. The legacy `feed_byte` adapter now owns its temporary view; its path does not receive the same saving.

The new `poll_parser` uses **80 B** and calls `consume_span`, which uses **136 B**, before further parser, wire and upper-layer calls. The 80 B is not the entire receive stack. The table's 192/224 B represents the largest linked individual function. The 1,024 B reservation does not validate a complete call chain, callbacks or nested interrupts.

## Choosing a Product Read Chunk

Configure the library with `-DXGL_DATALINK_RX_CHUNK_SIZE=AUTO|16|32|64|128`. AUTO selects 32 for Boot and 128 elsewhere. A smaller value changes neither MTU, RX cache nor wire capabilities. A conflicting consumer preprocessor macro cannot override an installed library.

Each due link reads once per step, bounded by both the runtime byte budget and chunk. A complete 128-byte frame needs at least four full reads with chunk32; short driver reads need more. Size the driver ring, polling frequency and total frame deadline together. Reading 32 bytes every millisecond has an ideal continuous ceiling of 32,000 B/s before burst and scheduling margin.

Report `rx_chunk_bytes` comes from the target-compiled layout probe. Policies match chunk, MTU/RX, profile and LTO; old reports without chunk identity cannot pass the new policies. Historical baselines explicitly retain chunk128, rather than treating different work capacities as identical conditions.

## Verification and Limits

New tests cover candidate replacement and clock wrap, absolute timeout, recovery after impossible lengths, authentication trailers, both CRC bytes, every span split, multiple frames and trailing partial data, corruption recovery, continuation after BUSY and borrowed input. Build contracts cover AUTO switching, installed headers, invalid values and macro conflicts. See repository `design/validation/datalink-optimization.md` for compiler, sanitizer, coverage and ARM evidence, and `design/adr/0008-datalink-runtime.md` for the decisions.

The probe contains no heap services or unresolved symbols. It omits real BSP, DMA, Flash programming, watchdog, update application and production crypto provider. Neither build reaches the 8 KiB Flash goal; MTU64 workspace is below 1 KiB. Remote CI, publication and hardware qualification remain separate checks.
