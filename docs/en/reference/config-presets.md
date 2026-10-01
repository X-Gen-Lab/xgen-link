# Configuration presets

A profile is a compile-time capability ceiling. A preset supplies explicit runtime limits within it. Unsupported capabilities are rejected.

## Profiles

| Profile | Authentication | Fragments / forwarding / out-of-order | xgen-memory libc default |
| --- | --- | --- | --- |
| boot | No | No | Off |
| embedded | Yes, 4 session slots | Yes | Off |
| full | Yes, 16 session slots | Yes | On |

## Runtime limits

| Preset | MTU | Window | Peers | Retained TX | Retained RX | Message bytes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| boot | 128 | 1 | 1 | 1 | 0 | Disabled |
| tiny | 128 | 2 | 1 | 2 | 2 | Disabled |
| small | 256 | 4 | 2 | 4 | 4 | 1024 |
| medium | 512 | 8 | 8 | 16 | 16 | 4096 |
| large / production | 1024 | 16 | 16 | 64 | 64 | 16384 |

Production additionally requires an authentication provider and explicit trusted sessions before traffic. The shared message budgets and reassembly slots also apply; inspect `xgl_feature_config_t` and query `xgl_memory_requirements()` for the actual selected configuration. A preset name is not a measured MCU RAM/Flash guarantee.
