# Config Presets

| Preset | Target | Auth | Notes |
| --- | --- | --- | --- |
| Tiny | Small MCU | Off by default | Minimum footprint |
| Small | Small app | Off by default | Larger buffers |
| Medium | Typical MCU | Off by default | Balanced resources |
| Large | Host / large MCU | Off by default | Larger windows and buffers |
| Production | Release profile | On by default | Requires auth provider |

Production releases should start from the Production preset and tighten resources for the target board.

## Exact Defaults

| Preset | ACK Timeout | Retry | Window | Max Frame | TX Pool | RX Buffer |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Tiny | 1000 ms | 3 | 2 | 128 | 1024 | 160 |
| Small | 1000 ms | 5 | 4 | 256 | 2048 | 288 |
| Medium | 1000 ms | 5 | 8 | 512 | 4096 | 544 |
| Large | 1000 ms | 7 | 16 | 1024 | 8192 | 1056 |
| Production | 1000 ms | 7 | 16 | 1024 | 8192 | 1056 |

All presets disable compression/encryption by default. Production defaults to `auth_required=true` and `auth_key_id=1`.
