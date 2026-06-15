# Send API

`xgl_send()` is the default send API.

## Data Ownership

`tx_data->data` is borrowed for the duration of the call. Reliable sends require stable retransmission storage, so the protocol keeps a copy or equivalent storage.

## Reliable and Unreliable

| Mode | Behavior |
| --- | --- |
| unreliable | Send without waiting for ACK |
| reliable | Allocate packet number, enter reliable queue, wait for ACK range/SACK |

## Fragmentation

If payload exceeds route MTU and fragmentation is enabled, FRAGMENT_EXT is used. Otherwise send fails.

## Callback

Application callbacks receive only payloads that passed CRC, authentication, routing, and reliability handling.

## Send Failure Handling

| Error | Common Cause | Handling |
| --- | --- | --- |
| `XGL_ERR_ROUTE_NOT_FOUND` | No route to target | Check route table |
| `XGL_ERR_BUFFER_TOO_SMALL` | Frame exceeds route MTU or caller buffer is too small | Reduce payload, enable fragmentation, or increase MTU |
| `XGL_ERR_WINDOW_FULL` | Reliable window is full | Retry later or increase window/queue budget |
| `XGL_ERR_NO_MEMORY` | Allocator/pool budget exhausted | Tune resource model |
| `XGL_ERR_TX_FAILED` | PHY send failed | Inspect driver and link |

## Application `data_type`

`data_type` classifies application payload. It is not the reliability ordering primitive and is not written into wire `packet_type`. When non-zero, it is encoded as DATA_TYPE_EXT in the header TLV area. Protocol control semantics use packet type and TLV extensions.
