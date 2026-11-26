# Integrate Boot

The Boot profile retains static workspaces, a single peer, at most one route and one packet per window. It supports reliable unicast with application-defined blocks. Other profiles provide authentication, forwarding, fragmentation, out-of-order buffering, statistics and detailed diagnostics.

## Select resources

Choose a [build profile](../reference/profiles.md) and use `xgl_config_get_preset_boot()` to set the runtime configuration. Set the route, nodes, MTU, RX buffer and time budget. Query target size and alignment through `xgl_memory_requirements()`, provide static storage and initialize with `xgl_init_static()`. Derive payload limits by subtracting the actual header, TLVs and CRC.

Compare equivalent Cortex-M0 probe ELFs and account for external PHY buffers, timing and call-chain stack. Set the runtime window and compiled window capacity separately. See [Validation](../development/validation.md) for resource evidence.

## Receive and commit

See host `boot_update` in [Runnable examples](../tutorials/examples.md). The application admission callback returns `XGL_ERR_BUSY` while the staging slot is unavailable. On acceptance it copies the block to application storage. The owner drives Flash work and sends a commit receipt after the callback returns. The sender advances its image offset after that receipt. After reset or failure, drain the old flow and resume under a new scope.

Observe driver admission, reliable ACK, durable commit and image activation separately. The product owns Flash partitions, signed-image verification, version policy, power-loss recovery and boot handoff. Keep one owner and an explicit borrow boundary for shared PHYs. Include the BSP, staging area and security components in budgets outside the protocol workspace.

## Qualification

Exercise backpressure, lost ACKs, broken byte streams, clock wraparound, resource exhaustion and shutdown. The host model validates transfer and admission. Hardware Flash, DMA and security qualification follow the product requirements.
