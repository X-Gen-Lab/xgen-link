# Security

Authentication is a compiled capability in embedded and full profiles. Boot and embedded-lite omit it. Authentication protects unicast traffic; this implementation does not provide payload encryption or a key-establishment protocol.

## Trusted Associations

Broadcast ID `0xFFFF` is rejected when installing an association and before authenticated serialization/signing. Rejection consumes neither a session slot nor a security sequence and never calls the provider or PHY. Plain broadcast remains available when the configured policy permits it.

The application explicitly installs `(remote_id, connection_id, session_epoch)` with directional TX/RX keys, nonce prefixes, TX initial sequence and RX minimum sequence. Received HELLO, SESSION metadata or a valid CRC never installs trust. Authenticated frames with unknown, closed or mismatched associations fail closed. Global `auth_required` requires authentication for all local traffic; the advanced `protect_installed_scopes` option requires it for exact installed or closed scopes. Closing an association permits plaintext only when both policies are disabled.

Installation governs subsequent frames. Out-of-order frames admitted before installation retain their original security disposition and may still be delivered afterward. For a business trust transition, use a new trusted epoch/scope, install it before admitting its traffic, and explicitly dispose of previously admitted old-scope data. Installation does not replace transport queue cleanup or business-session establishment.

Closing a security session keeps a tombstone. Installing reserves each directional `(key_id, nonce_prefix)` domain immediately. Reinstalling the same scope or reusing a reserved domain in the same security context is rejected; closed slots are not recycled as fresh security slots. Capacity is a build limit: four sessions for embedded and sixteen for full. The library compares key IDs, not provider key material. The application must prevent different IDs from aliasing the same actual key with a reused prefix. Fresh trusted keys or persistent freshness state are required across process/MCU restart; clearing RAM with fixed keys and prefixes permits nonce reuse.

## Authentication Attempt

SECURITY value is `LE32(key_id) || LE64(security_seq) || u8(tag_length)`. The provider receives versioned `xgl_auth_input_t`, including the exact endpoints, connection, epoch, sequence and a 12-byte nonce: `BE32(installed_directional_prefix) || BE64(security_seq)`.

AAD is the entire encoded header and TLVs, with TTL byte 6 and header-CRC bytes 22–23 zeroed. Every other AAD byte and every payload byte is authenticated. Tags have the provider's fixed declared length. A sign attempt reserves a new sequence before the provider call; failure does not roll it back. `UINT64_MAX` can be used once, then the association is exhausted. Reliable retransmission keeps DATA numbering but always signs a newly composed frame. Production send uses the validated frame layout to locate SECURITY and the tag without rescanning encoded TLVs.

## Receive and Forwarding

CRC/layout validation precedes authentication. Replay is checked against a candidate 64-bit receive window; only successful tag verification commits that candidate. Duplicate authentication sequences are rejected, including byte-identical replay. A legitimate retransmission uses a fresh security sequence and is deduplicated later by transport DATA numbering.

Local network delivery verifies security before transport mutation. A forwarding node changes TTL and CRCs while preserving the end-to-end tag; TTL is excluded from AAD by canonicalization. RESET and transport close never reset replay or TX security counters. Callbacks are synchronous and must not reenter or destroy the active instance.

Verification: `test/unit/security/test_security.cpp`.
