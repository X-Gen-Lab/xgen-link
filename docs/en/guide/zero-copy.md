# Zero-Copy

`xgl_send_zerocopy_at()` builds a single unreliable frame in caller-owned writable storage. It does not introduce asynchronous buffer ownership.

## Supported Scope

The current API supports single-frame, unreliable transmission. `reliable=true` is rejected with `XGL_ERR_INVALID_PARAM`; use `xgl_send_at()` for reliable or fragmented messages.

The zero-copy descriptor has no connection/epoch fields and uses the default scope. Authenticated use requires a trusted session for that scope. Use the regular send API when explicit connection/epoch selection is needed.

## Buffer Layout

Reserve the exact header offset before payload and trailer capacity after it:

| Part | Bytes |
| --- | ---: |
| Base header | `XGL_FRAME_HEADER_SIZE` (24) |
| Nonzero data type | `XGL_DATA_TYPE_EXT_SIZE` (3) |
| Authentication security extension, if enabled | 15 |
| Authentication tag, if enabled | Provider tag length |
| Final CRC | `XGL_CRC16_SIZE` (2) |

`data_offset` must equal the base header plus enabled extensions. `buffer_size` must cover offset, payload, tag, and CRC without overflow; the full serialized frame must fit the route MTU. The stack writes header and CRC, so do not construct them separately.

## Ownership

The caller may reuse storage after `xgl_send_zerocopy_at()` returns. PHY TX must finish reading or copying it before returning, even when backed by DMA. This interface reduces frame assembly copies; a driver's private DMA copy may still be necessary.

## Failure Handling

Check the returned status. Do not pass immutable memory, omit required headroom, or retain callback pointers. A local success is not evidence of remote delivery. Calls and recovery must follow the same serialization rules as normal sending.
