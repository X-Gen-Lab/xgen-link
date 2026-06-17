# Validation Matrix

The validation matrix links protocol capability, risk, and required tests. Before release, each row should have automated coverage or an explicit manual verification record.

## Core Protocol Matrix

| Capability | Risk | Must verify | Recommended tests |
| --- | --- | --- | --- |
| v2 base header | Wrong offset, endian, or CRC coverage | 24-byte offsets, little-endian fields, zeroed CRC field during calculation | `test/test_wire.cpp`, `test/test_frame.cpp` |
| TLV cursor | Overrun, zero-length confusion, unknown extension mishandling | Multiple extensions, empty extension area, invalid length, header_len overrun | `test/test_wire.cpp`, `test/test_parser.cpp` |
| Parser resync | Noise causes lockup or false frame | Noise, overlapping magic, fragmented input, consecutive frames | `test/test_parser.cpp` |
| Auth trailer | Unauthenticated frame bypass, tag length mismatch | missing provider, tampered header/payload/tag, authenticated zero-copy | `test/test_security.cpp`, `test/test_datalink.cpp`, `test/test_send.cpp` |
| Replay window | Replay attack, cross-connection pollution, lost ACK recovery | source/connection/session/packet isolation, non-reliable duplicate rejection, reliable duplicate ACK recovery | `test/test_security.cpp`, `test/test_datalink.cpp` |
| Route forwarding | TTL/auth AAD conflict, MTU overrun | TTL decrement, CRC recompute, auth tag preserved, route MTU reject | `test/test_network.cpp` |
| Reliable queue | Wrong ACK release, lost SACK hole | ACK range release, SACK fast retransmit, retry limit | `test/test_transport.cpp`, `test/test_reliable.cpp` |
| Peer state | Multiple connections pollute each other | peer key isolation by node/connection/session | `test/test_transport.cpp` |
| Ordered delivery | Out-of-order duplicate delivery | out-of-order buffering, contiguous advancement, duplicate filtering | `test/test_transport.cpp` |
| Fragmentation | Memory exhaustion, cross-session mixing | `FRAGMENT_EXT` reassembly, budget, timeout, reset scope | `test/test_fragment.cpp` |
| Low-power deadline | Sleep misses retransmit/reassembly deadline | nearest route/reliable/reassembly deadline | `test/test_instance.cpp` |
| No-heap profile | Hidden malloc and fragmentation | no-heap smoke and allocator failure paths | `tools/noheap_smoke.c`, memory tests |
| Examples build | README drift from compilable examples | echo server, file transfer, and multi-node targets build | `examples/CMakeLists.txt`, `xgl_release_validation` |
| Low-power runtime guide | Documentation diverges from runtime deadline API | `xgl_next_deadline_ms()` and route polling deadline behavior | `test/test_instance.cpp`, `test/test_time_provider.cpp` |
| Porting guide | Platform assumptions break new board ports | PHY callbacks, time provider, mutex/noop mutex behavior | `test/test_platform.cpp`, `test/test_time.cpp`, `test/test_mutex.cpp` |
| Resource model | Preset budgets drift from config macros | preset tx/rx/window/frame values and no-heap behavior | `test/test_config.cpp`, `test/test_types.cpp`, `test/test_footprint.cpp` |
| Documentation build | Broken links or stale Doxygen public API | strict MkDocs plus Doxygen API generation | `docs/CMakeLists.txt`, `.github/workflows/pages.yml` |

## Fuzz / Stress Recommendations

| Scenario | Input model | Pass criteria |
| --- | --- | --- |
| Parser random bytes | Random byte stream with legal and semi-legal frames inserted | no crash, no overrun, recover to next legal magic |
| TLV malformed | Random ext_type/ext_len/header_len | invalid TLVs dropped, valid TLVs parsed correctly |
| Auth tamper | Mutate header, extension, payload, or tag bytes | rejected whenever a frame declares authentication; unauthenticated frames are rejected when auth_required is enabled |
| Route storm | route changes, TTL boundary, MTU boundary | no expired TTL forwarding, no over-MTU frame send |
| Lossy transport | Inject loss, reorder, duplicate, and delay | reliable packets delivered in order or fail by retry limit |
| Fragment attack | Large message, overlapping ranges, missing fragments, timeout | budgets are not exceeded and timeout releases resources |

## Release Gate

Recommended sequence:

```sh
cmake --preset gcc-test
cmake --build build/gcc-test --target xgl_tests
ctest --preset gcc-test --output-on-failure
cmake --build build/gcc-test --target xgl_release_validation
cmake --preset ci
cmake --build build/ci --target xgl_docs
```

Release environments must install `cppcheck`. Unavailable static analysis is not an acceptance condition.

## Documentation Consistency

- Node IDs in docs must be `uint16_t`.
- Packet numbers in docs must be `uint32_t`.
- Wire headers in docs must be the v2 24-byte header.
- Unimplemented capabilities must be documented as reserved and state whether production paths reject or disable them.
- Public API docs describe stable SDK entry points only; internal state structures are not stable ABI.
