# Security Model

Production configuration requires authentication by default. Tests and debug builds may explicitly disable it, but release profiles should enable `auth_required`.

## Auth Provider

`xgl_auth_provider_t` provides:

- `sign`
- `verify`
- `tag_len`, capped by `XGL_AUTH_TAG_MAX_LEN`
- `user_data`

Initialization must fail when `auth_required=true` and the provider is missing, `tag_len == 0`, or `tag_len > XGL_AUTH_TAG_MAX_LEN`.

## AAD and Payload

The base header and extensions are authenticated as AAD. Payload is authenticated but not encrypted. CRC provides fast error detection; the authentication tag prevents forgery, tampering, and replay.

## Authentication Trailer

The authentication trailer is placed after payload and before frame CRC. SECURITY_EXT records `key_id`, `nonce_id`, and `tag_len`. The provider declares a fixed tag length so the signing path does not need a trial signature followed by a second signature.

## Verification Order

1. Check magic, version, header_len, and payload_len.
2. Verify header CRC.
3. Parse SECURITY_EXT.
4. Verify authentication trailer.
5. Check replay window.
6. Enter network/transport semantic handling.

Any failure prevents payload delivery.

## Replay Window

Anti-replay key:

```text
source_id + connection_id + session_epoch + packet_number
```

Duplicate packets, old-session packets, and wrong-connection packets are not ACKed or delivered.

## Multi-Hop Forwarding

TTL is a hop-mutable field. After forwarding changes TTL, the next hop must still validate the frame: either through regenerated hop-level authentication material or an explicit mutable-header policy. The implementation must regenerate the required CRC/auth material on forwarding paths.

## Reserved

Encryption is reserved. Do not treat `enable_encryption` as an available production encryption path.

## Key Boundary

XGL does not persist keys or define key derivation. Production applications implement key storage, rotation, key id mapping, and hardware security module integration inside the auth provider.
