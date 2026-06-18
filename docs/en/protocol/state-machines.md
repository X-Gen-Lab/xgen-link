# Protocol State Machines

This page describes the core XGL v2 state machines. They are the shared language for implementation, testing, and debugging.

## Parser State Machine

```mermaid
stateDiagram-v2
  [*] --> SearchMagic
  SearchMagic --> SearchMagic: noise byte
  SearchMagic --> BaseHeader: A5 5A
  BaseHeader --> SearchMagic: version/header_len/crc invalid
  BaseHeader --> Extensions: header_len > 24
  BaseHeader --> Body: header_len == 24
  Extensions --> SearchMagic: TLV invalid or SECURITY_EXT missing when required
  Extensions --> Body: all TLVs valid
  Body --> SearchMagic: payload/auth/crc invalid
  Body --> FrameReady: payload + optional auth trailer + frame crc complete
  FrameReady --> SearchMagic: xgl_parser_get_frame + reset
```

Requirements:

- `SearchMagic` must handle noise and overlapping magic.
- `BaseHeader` reads only the 24-byte base header and must not trust payload early.
- `Extensions` walks TLVs with a cursor; any overrun resets the parser.
- `Body` length is derived from `payload_len + auth_tag_len + frame_crc16`.

### Parser State Table

| State | Entered when | Completes when | Error/timeout behavior | Evidence |
| --- | --- | --- | --- | --- |
| `XGL_PARSE_MAGIC` | Parser is reset or a bad frame is dropped | `A5 5A` is found | Noise is ignored; overlapping magic is retained | `src/wire/xgl_parser.c`, `test/test_parser.cpp` |
| `XGL_PARSE_HEADER` | First magic byte has been cached | 24-byte base header decodes successfully | Bad magic/version/header CRC resets to MAGIC | `src/wire/xgl_parser.c`, `src/wire/xgl_wire.c`, `test/test_parser.cpp` |
| `XGL_PARSE_PAYLOAD` | Header/TLVs are valid and body length is non-zero | `payload_len + auth_tag_len` bytes are cached | Cache overflow resets to MAGIC | `src/wire/xgl_parser.c`, `test/test_parser.cpp` |
| `XGL_PARSE_CRC` | Header-only frame or body bytes are complete | Frame CRC validates | CRC failure resets to MAGIC | `src/wire/xgl_parser.c`, `test/test_parser.cpp` |

`xgl_parser_check_timeout()` only expires non-MAGIC states. A timeout resets the
parser to MAGIC and clears cached length, expected payload length, and expected
authentication tag length.

## Datalink Validation State Machine

```mermaid
flowchart TD
  Ready[FrameReady] --> Decode[Decode wire header]
  Decode --> Ext[Parse extensions]
  Ext --> AuthReq{auth_required?}
  AuthReq -- yes --> HasSec{SECURITY_EXT valid?}
  HasSec -- no --> DropAuth[Drop auth error]
  HasSec -- yes --> Verify[Verify auth trailer]
  AuthReq -- no --> Replay[Replay check if authenticated]
  Verify --> AuthOk{valid?}
  AuthOk -- no --> DropAuth
  AuthOk -- yes --> Replay
  Replay --> ReplayOk{new packet?}
  ReplayOk -- no --> DropReplay[Drop replay]
  ReplayOk -- yes --> Network[Pass to network]
```

Authentication failure, rejected replay, and key mismatch must not be ACKed and must not enter transport. ACK-eliciting reliable duplicates are the replay exception: they may enter transport only to regenerate ACK/SACK and must not be delivered again.

## Network Forwarding State Machine

```mermaid
stateDiagram-v2
  [*] --> InspectTarget
  InspectTarget --> LocalDelivery: target is local
  InspectTarget --> CheckTTL: target is remote
  CheckTTL --> DropTTL: ttl <= 1
  CheckTTL --> LookupRoute: ttl > 1
  LookupRoute --> DropNoRoute: route missing
  LookupRoute --> CheckMTU: route found
  CheckMTU --> DropMTU: serialized frame > route mtu
  CheckMTU --> RewriteMutable: fits
  RewriteMutable --> RecomputeCRC: ttl decremented
  RecomputeCRC --> Forward: header/frame CRC updated
  Forward --> [*]
  LocalDelivery --> [*]
```

TTL is a mutable header field. Forwarding must recompute header CRC and frame CRC, but it must preserve the end-to-end authentication tag. Authenticated frames remain verifiable because TTL and header CRC are canonicalized out of the end-to-end AAD.

## Transport Send State Machine

```mermaid
stateDiagram-v2
  [*] --> Prepare
  Prepare --> AssignPacket: send accepted
  AssignPacket --> Fragment: payload exceeds route payload budget
  AssignPacket --> QueueReliable: reliable single frame
  Fragment --> QueueReliable: reliable fragment
  Fragment --> SendUnreliable: unreliable fragment
  QueueReliable --> SendFrame
  SendUnreliable --> SendFrame
  SendFrame --> AwaitAck: reliable frame sent
  SendFrame --> Done: unreliable frame sent
  AwaitAck --> Done: ACK range covers packet
  AwaitAck --> Retransmit: timeout or SACK hole
  Retransmit --> SendFrame
  AwaitAck --> Failed: retry limit exceeded
```

Constraints:

- Packet numbers are monotonic per peer key.
- Reliable payload copies cannot be released before ACK coverage.
- Retry failure affects only the scoped peer/connection/session.

## Transport Receive State Machine

```mermaid
flowchart TD
  Packet[Local packet] --> Scope[Resolve peer key]
  Scope --> Type{packet type}
  Type -- ACK/CONTROL --> Control[Process ACK range/SACK/reset/close]
  Type -- DATA --> Number{packet_number}
  Number -- "< rx_next" --> Duplicate[Drop duplicate]
  Number -- "== rx_next" --> Deliver[Deliver payload or fragment]
  Number -- "> rx_next" --> Buffer[Cache out-of-order]
  Deliver --> Drain[Drain contiguous buffered packets]
  Buffer --> AckSack[Send ACK/SACK]
  Drain --> Ack[Send ACK]
```

The application callback receives only ordered, complete, authenticated, budget-compliant payloads.

## Fragment Reassembly State Machine

```mermaid
stateDiagram-v2
  [*] --> NoMessage
  NoMessage --> AllocMessage: first FRAGMENT_EXT
  AllocMessage --> Receiving: budget reserved
  Receiving --> Receiving: non-overlapping range accepted
  Receiving --> Complete: all ranges covered
  Receiving --> DropMessage: timeout or budget error
  Complete --> DeliverAndFree
  DeliverAndFree --> [*]
  DropMessage --> [*]
```

Reassembly key:

```text
source_id + connection_id + session_epoch + message_id
```

The key prevents the same `message_id` from being mixed across nodes, connections, or sessions.

## Reset/CLOSE Scope

RESET and CLOSE must only clear the matching scope:

```text
target/source node + connection_id + session_epoch
```

They must not globally clear the route table, other peers' reliable queues, other sessions' replay windows, or unrelated fragment reassembly state.

## Parser Timeout Behavior

The parser state machine includes a timeout guard during frame reception.

### Timeout Parameter

| Constant | Default | Description |
| --- | --- | --- |
| `XGL_PARSER_TIMEOUT_MS` | 1000 | Parser per-frame reception timeout (ms) |

### Timeout Trigger Conditions

- Parser has found magic and started parsing, but the complete frame is not received within `XGL_PARSER_TIMEOUT_MS`.
- No new magic appears for an extended period in a continuous byte stream.

### Timeout Behavior

1. Discard incomplete frame data already received.
2. Reset the parser state machine to `SearchMagic`.
3. Increment the counter.
4. Continue searching for magic from the next byte.

### Authentication Tag Length

Parser maintains `expected_auth_tag_len` to correctly locate the auth trailer and frame CRC boundary when the `AUTHENTICATED` flag is set.

### Evidence

`include/xgl/internal/xgl_parser.h`, `src/wire/xgl_parser.c`
