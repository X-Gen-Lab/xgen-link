# Examples

## Programs

| Target | Demonstration |
| --- | --- |
| `echo_server` | Queue in RX callback, respond from the next application step |
| `file_transfer` | Reliable application chunks and payload verification |
| `multi_node` | Routing through an intermediate node; requires forwarding |
| `boot_update` | Bounded blocks, slow Flash BUSY and lost-ACK recovery |

The examples link `xgl::xgl` and provide explicit time and fixed application storage. `boot_update` also uses private byte and CRC helpers. The common synchronous host PHY copies frames into bounded byte queues.

## Run

Enable `XGL_BUILD_EXAMPLES=ON`. Set `XGL_BUILD_TESTS` or `XGL_BUILD_NOHEAP_SMOKE` to register the examples with CTest. Boot excludes the forwarding example. [Run your first link](first-link.md) gives the echo commands. See `examples/*/README.md` for the other executables and their limits.
