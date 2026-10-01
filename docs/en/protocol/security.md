# Security

Authentication is an optional compiled capability in embedded and full profiles. The current Boot profile omits it. Authentication protects unicast traffic; this implementation does not provide payload encryption or a key-establishment protocol.

## Trusted Associations

The application explicitly installs `(remote_id, connection_id, session_epoch)` with directional TX/RX keys, nonce prefixes, TX initial sequence and RX minimum sequence. Received HELLO, SESSION metadata or a valid CRC never installs trust. Authenticated frames with unknown, closed or mismatched associations fail closed. Plain frames are governed by `auth_required`: when it is false, closing an association does not prohibit plaintext traffic.

Closing a security session keeps a tombstone. Installing reserves each directional `(key_id, nonce_prefix)` domain immediately. Reinstalling the same scope or reusing a reserved domain in the same security context is rejected; closed slots are not recycled as fresh security slots. Capacity is a build limit: four sessions for embedded and sixteen for full. The library compares key IDs, not provider key material. The application must prevent different IDs from aliasing the same actual key with a reused prefix. Fresh trusted keys or persistent freshness state are required across process/MCU restart; clearing RAM with fixed keys and prefixes permits nonce reuse.

## Authentication Attempt

SECURITY value is `LE32(key_id) || LE64(security_seq) || u8(tag_length)`. The provider receives versioned `xgl_auth_input_t`, including the exact endpoints, connection, epoch, sequence and a 12-byte nonce: `BE32(installed_directional_prefix) || BE64(security_seq)`.

AAD is the entire encoded header and TLVs, with TTL byte 6 and header-CRC bytes 22–23 zeroed. Every other AAD byte and every payload byte is authenticated. Tags have the provider's fixed declared length. A sign attempt reserves a new sequence before the provider call; failure does not roll it back. `UINT64_MAX` can be used once, then the association is exhausted. Reliable retransmission keeps DATA numbering but always signs a newly composed frame.

## Receive and Forwarding

CRC/layout validation precedes authentication. Replay is checked against a candidate 64-bit receive window; only successful tag verification commits that candidate. Duplicate authentication sequences are rejected, including byte-identical replay. A legitimate retransmission uses a fresh security sequence and is deduplicated later by transport DATA numbering.

Local network delivery verifies security before transport mutation. A forwarding node changes TTL and CRCs while preserving the end-to-end tag; TTL is excluded from AAD by canonicalization. RESET and transport close never reset replay or TX security counters. Callbacks are synchronous and must not reenter or destroy the active instance.

Verification: `test/unit/security/test_security.cpp`.
