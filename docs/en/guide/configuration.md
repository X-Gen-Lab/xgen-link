# Configuration

Configuration starts with `xgl_config_t`. Prefer a preset first, then override board-specific details.

## Required Fields

- `source_id`: local 16-bit node ID.
- `route_table` and `route_table_len`: at least one route for reachable targets.
- `rx_callback`: required when the application receives payloads.

## Route Item

| Field | Description |
| --- | --- |
| `target_id` | Target node ID |
| `phy` | TX/RX callback set |
| `max_frame_size` | Complete frame length allowed by the route |
| `read_freq_hz` | Polling rate for the PHY |
| `metric` | Cost metric for route selection/replacement |

`max_frame_size` must fit base header, extensions, payload, authentication trailer, and CRC.

## Authentication

The production preset enables `auth_required`. Configure:

- `auth_key_id`
- `auth_provider`
- `auth_provider->tag_len`, with `0 < tag_len <= XGL_AUTH_TAG_MAX_LEN`

## Memory

`memory.allocator` controls protocol memory. Strict MCU profiles should disable fallback malloc and provide enough pool storage during initialization.

| Field | Description |
| --- | --- |
| `tx_pool_size` | TX pool budget |
| `rx_buffer_size` | Parser/RX cache budget |
| `allocator` | Custom allocator; NULL behavior is controlled by `XGL_ALLOW_FALLBACK_MALLOC` |

## Feature Flags

Compression and encryption are reserved. Production builds should not enable codec features until payload expansion and security models are wired.

## Validation Guidance

- `source_id` must not be 0 or reserved.
- `window_size` must fit reliable queue and MCU RAM budgets.
- Evaluate `max_retry_count` with low-power sleep intervals.
- Production preset must provide an auth provider before create/init.
