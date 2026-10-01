# Architecture

XGen Link owns protocol state. xgen-memory provides allocation services and fixed pools; xgen-containers provides generic containers and bitsets; xgen-bytes, xgen-crc, and xgen-status provide byte encoding, CRC algorithms, and generic statuses. The application owns the clock, execution context, PHY driver and trusted key establishment.

## Module Boundaries

| Module | Responsibility | Boundary |
| --- | --- | --- |
| API | Validate ABI/profile/capacities; construct instances; drive work | Public configuration and explicit timestamps |
| Transport | Exact peer ownership; reliable DATA; ACK; retry; optional messages | Typed logical packet views |
| Network | Route lookup, TLV composition, local authentication, forwarding | Logical packets above, complete frames below |
| Security | Installed directional nonce domains, signing and replay state | Versioned authentication input |
| Datalink | Incremental stream framing and synchronous PHY submission | Borrowed byte spans |
| Wire | One canonical frame parser, serializer and extension encoding | Bounded input and output spans |
| Protocol memory | Reserve resources by their protocol lifetime | Explicit xgm allocation services |

## Send and Receive Flow

```mermaid
flowchart LR
    A[Application] --> T[Transport]
    T --> N[Network]
    N --> W[Frame composition and authentication]
    W --> D[Datalink]
    D --> P[Synchronous PHY]
```

Receive reverses the boundary direction: each PHY parser calls wire to validate a complete frame and produce a borrowed view. Datalink and network pass that view through; network verifies local authentication before transport changes peer state. The complete-frame CRC is computed once. Internal raw-frame entry points still validate their inputs before delivery.

Copied, authenticated and in-place sends share frame measurement and encoding rules, with datalink as the single PHY submission owner. Forwarding creates no local transport state but uses that same PHY exit. Reliable retransmission starts from an owned logical packet and traverses frame composition again, retaining its DATA number while obtaining a fresh security sequence.

## Ownership and Execution

One peer owns one exact `(remote_id, connection_id, session_epoch)` scope, including its window, wait-ACK records, RTT and receive order. Borrowed packet/frame views never outlive a synchronous call; retained payloads have a resource-class owner. Security state is separate from peer lifetime and survives transport RESET.

The instance owns one security context, borrowed by the layers. Each unique PHY descriptor owns one parser and RX cache; multiple routes sharing that descriptor do not reserve duplicates. A completed reassembly transfers as an owned object carrying its length. Application BUSY retains the original byte admission charge until a common release operation returns it.

Calls on one instance are serialized by the caller. Callbacks cannot reenter or destroy that instance. No global clock, internal mutex wrapper, implicit heap fallback or generic container implementation is provided by the protocol library. See [Memory](memory.md) and [Platform](platform.md).

## Source and Build Boundaries

`include/xgl/` contains only the public SDK. Private headers live with their implementations under `src/api`, `wire`, `datalink`, `network`, `transport` and `security`. Cross-layer data and resource contracts live under `src/internal`. Private directories are neither installed nor exported to consumers.

Production still exports one `xgl::xgl` target. `cmake/XglSources.cmake` owns explicit source selection and profile trimming, `XglInstall.cmake` owns SDK packaging, and `XglDevelopment.cmake` owns development checks. Tests live under `test/unit/<layer>`, `integration`, `property` and `support`, preserving historical case names and replay seeds.
