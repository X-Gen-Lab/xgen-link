# Configuration presets

A profile is a compile-time capability ceiling. A preset supplies explicit runtime limits within it. Unsupported capabilities are rejected.

## Profiles

| Profile | Authentication | Forwarding / out-of-order | libc fallback default |
| --- | --- | --- | --- |
| boot | No | No | Off |
| embedded-lite | No | Out-of-order only | Off |
| embedded | Yes, 4 session slots | Yes | Off |
| full | Yes, 16 session slots | Yes | On |

## Compiled resource identity

`XGL_TRANSPORT_WINDOW_CAPACITY` can be 1, 8, 16, 32, 64 or 128, with default 128. `XGL_RELIABLE_INDEX_BUCKETS` can be 1, 2, 4, 8, 16 or 32. Defaults are 1 for Boot, 8 for Embedded Lite and 32 otherwise. `XGL_DATALINK_RX_CHUNK_SIZE` can be 16, 32, 64 or 128 bytes, with defaults 32 for Boot and 128 otherwise. Adaptive RTO defaults on and advanced runtime defaults off.

`XGL_DATALINK_MAX_FRAME_SIZE` selects the producer's complete-frame ceiling from 64 to 2048 bytes and defaults to 2048. The instance MTU must fit this ceiling. The generated header and installed target record profile, resource selection and build identity. Check the actual exports and keep application macros consistent with the SDK. Boot requires libc fallback to be off.

## Runtime limits

| Preset | MTU | Window | Peers | Retained TX | Retained RX |
| --- | ---: | ---: | ---: | ---: | ---: |
| boot | 128 | 1 | 1 | 1 | 0 |
| tiny | 128 | 2 | 1 | 2 | 2 |
| small | 256 | 4 | 2 | 4 | 4 |
| medium | 512 | 8 | 8 | 16 | 16 |
| large / production | 1024 | 16 | 16 | 64 | 64 |

Production additionally requires an authentication provider and explicit trusted sessions before traffic. Inspect `xgl_feature_config_t` and query `xgl_memory_requirements()` for the actual selected configuration. A preset name is not a measured MCU RAM/Flash guarantee.
