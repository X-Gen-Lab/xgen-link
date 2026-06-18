# Datalink Layer

The datalink layer bridges the physical layer (PHY) and the network layer in the XGL protocol stack. It handles frame boundary detection, authentication verification, anti-replay checking, and frame serialization/deserialization.

## Design Goals

- **Frame integrity**: The parser state machine converts byte streams into complete frames; CRC verification ensures data correctness.
- **Authentication front-loading**: Auth verification and anti-replay checks are completed before entering the network layer, protecting upper layers from forged frames.
- **Layer decoupling**: Interacts with the network layer through `xgl_layer_interface_t` without holding a direct reference to the network layer context.

## Context Structure

```text
xgl_datalink_ctx_t
├── parser              (xgl_parser_t — frame parser state machine)
├── rx_cache            (uint8_t* — receive cache buffer)
├── rx_cache_size       (size_t — cache size)
├── stats               (xgl_layer_stats_t* — layer statistics)
├── error_callback      (xgl_error_callback_t — error callback)
├── source_id           (uint16_t — local node ID)
├── auth_required       (bool — require authentication)
├── auth_key_id         (uint32_t — active auth key ID)
├── auth_provider       (xgl_auth_provider_t* — auth callback)
├── replay_windows[16]  (xgl_replay_window_t — anti-replay windows)
├── replay_window_used[16] (bool — slot occupancy flags)
└── upper_layer         (xgl_layer_interface_t* — upper layer interface)
```

## Parser State Machine

The parser converts byte streams from PHY into complete frames through 4 states:

1. **MAGIC**: Scanning for the frame start magic byte sequence.
2. **HEADER**: Receiving and parsing the 24-byte base header and TLV extensions.
3. **PAYLOAD**: Receiving `payload_len` bytes of payload data.
4. **CRC**: Verifying the frame CRC16.

### Timeout

- Default: `XGL_PARSER_TIMEOUT_MS = 1000` ms
- On timeout, the parser resets to MAGIC state, discarding all cached data.
- Prevents resource exhaustion from incomplete frames (slow-connection or attack).

### Key Fields

| Field | Type | Description |
| --- | --- | --- |
| `state` | xgl_parse_state_t | Current parser state |
| `cache` | uint8_t* | Frame data cache |
| `expected_header_len` | size_t | Expected header length |
| `expected_payload_len` | uint16_t | Expected payload length |
| `expected_auth_tag_len` | uint8_t | Expected auth trailer length |

## Receive Path

### Verification Order

1. **Magic check**: Confirm frame start marker.
2. **Header CRC**: Verify `header_crc16` (excluding `ttl` and `header_crc16` itself).
3. **Payload length**: Confirm `payload_len` is within valid range.
4. **Frame CRC16**: Verify the full-frame CRC16 (after auth trailer).
5. **Auth verification**: When `auth_required=true` or frame declares authentication, verify auth trailer.
6. **Replay check**: For authenticated frames, check the replay window.
7. **Deliver upward**: Pass validated frame to network layer via `xgl_layer_receive()`.

**Any failure at any step drops the frame without delivering the payload.**

## Send Path

1. Receive frame structure from the network layer.
2. Serialize header and TLV extensions.
3. Calculate CRC16 over header + extensions + payload.
4. If authentication is required, append auth trailer.
5. Send serialized frame through the PHY callback.

## Anti-Replay Windows

### Slot Management

- Fixed 16 slots (`XGL_DATALINK_REPLAY_WINDOW_COUNT = 16`)
- 64-bit bitmap per slot (`XGL_DATALINK_REPLAY_WINDOW_SIZE = 64`)
- Slots keyed by `(source_id, connection_id, session_epoch)`

### Three-State Determination

| State | Condition | Behavior |
| --- | --- | --- |
| NEW | First time seeing this session; allocate free slot | Accept, set bitmap bit |
| VALID | packet_number within window and bitmap bit is 0 | Accept, set bitmap bit |
| DUPLICATE | packet_number within window and bitmap bit is 1 | Drop, count |
| OUT_OF_WINDOW | packet_number outside window range | Drop |

### Capacity

16 slots × 64 bits = supports up to 16 concurrent authenticated connections, each tracking 64 in-flight packets. Sufficient for typical MCU deployments.

## PHY Error Classification

| Error Type | Handling | Counter |
| --- | --- | --- |
| CRC16 failure | Drop frame | `rx_crc16_errors` |
| Header CRC failure | Drop frame | `rx_header_crc_errors` |
| Auth verification failure | Drop frame + error_callback | `rx_auth_failures` |
| Replay rejection | Drop frame | `rx_replay_duplicates` |
| Parser timeout | Reset parser | — |
| Parser cache overflow | Reset parser | — |

## Layer Relationships

```text
Transport ←→ Network ←→ Datalink ←→ PHY
                            ↑
                        xgl_layer_interface_t
```

- **Upper layer (Network)**: Delivers validated frames through the `upper_layer` interface.
- **Lower layer (PHY)**: Byte-level send/receive through `xgl_phy_ops_t` callbacks.
- **Security**: Built-in replay window using `xgl_security.h` algorithms.

## Evidence

| Rule | Source | Test |
| --- | --- | --- |
| Parser state machine | `src/wire/xgl_parser.c` | `test/test_parser.cpp` |
| Auth verify + replay | `src/datalink/xgl_datalink_receive.c` | `test/test_datalink.cpp` |
| Frame serialization | `src/datalink/xgl_datalink_send.c` | `test/test_datalink.cpp` |
| Replay window | `src/security/xgl_security.c` | `test/test_security.cpp` |