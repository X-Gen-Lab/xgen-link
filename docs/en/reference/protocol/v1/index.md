# Protocol Overview

XGen Link implements Wire v1 for bounded, synchronous embedded communication. All profiles use the same header and extension encoding. Each DATA submission fits in one frame. Peers and relays must use version 1.

## Responsibilities

| Component | Responsibility |
| --- | --- |
| API | Instance lifecycle, explicit time, configuration and workspace checks |
| Transport | Peer ownership, reliable DATA, ACK/SACK and retries |
| Network | Destination lookup, extension composition and optional forwarding |
| Security | Explicit trusted sessions, fresh authentication sequences and replay |
| Datalink / Wire | Synchronous frame submission, bounded parsing, layout and CRC |

## Core Contracts

A peer is exactly `(remote_id, connection_id, session_epoch)`. Zero connection and epoch are ordinary values. There is no short-session fallback. Only reliable DATA consumes the contiguous 32-bit DATA sequence. ACK, CONTROL and unreliable DATA carry zero in that header field. Authentication uses a separate 64-bit sequence for every signing attempt.

Successful reliable submission means local acceptance into owned storage. An ACK means transport or application acceptance; it does not prove that flash programming or another business operation has committed. Failure is terminal for an epoch. RESET cannot reopen it; explicit close releases capacity before a new epoch is established.

## Reading Order

Read [Architecture](../../../explanation/architecture.md), [Wire Format](wire-format.md), [Extensions](extensions.md), [Reliability](reliability.md) and [Security](security.md) for the principal contracts. [Memory](../../../explanation/resource-model.md), [Platform](../../../guides/porting.md) and [Implementation Map](../../../explanation/implementation-map.md) describe capacity, execution and source ownership. The Boot profile uses one peer and a one-packet window; firmware transfer chunking belongs to the Boot application protocol.
