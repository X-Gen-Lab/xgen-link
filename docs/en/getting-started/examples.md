# Examples

## Programs

| Target | Demonstration |
| --- | --- |
| `echo_server` | Queue in RX callback, respond from the next application step |
| `file_transfer` | Reliable application chunks and payload verification |
| `multi_node` | Routing through an intermediate node; requires forwarding |
| `boot_update` | Bounded blocks, slow Flash BUSY and lost-ACK recovery |

All examples use public APIs, explicit time and fixed application storage. The common synchronous host PHY copies frames into bounded byte queues.

## Run

Enable `XGL_BUILD_EXAMPLES=ON`, build and run CTest. Boot excludes the forwarding example. Executable paths and limits are described in `examples/*/README.md`.
