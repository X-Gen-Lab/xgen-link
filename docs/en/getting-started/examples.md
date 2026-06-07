# Examples

Examples live under `examples/`.

| Example | Purpose |
| --- | --- |
| `echo_server` | Basic send/receive and echo behavior between two 16-bit nodes |
| `multi_node` | Three-node routing and forwarding |
| `file_transfer` | Reliable transfer and fragmentation |
| `platforms` | Bare-metal, FreeRTOS, and Windows mock ports |

Build:

```sh
cmake --preset gcc-test
cmake --build build/gcc-test
```

Example callbacks must use `uint16_t source_id`, matching the public `xgl_rx_callback_t` signature.

`echo_server` is not a single-node self-loop. It creates node `1` and node `2`
as separate protocol instances connected by two simulated PHY channels, keeping
the example aligned with the production rule that ordinary unicast frames do not
use the same source and target node ID.
