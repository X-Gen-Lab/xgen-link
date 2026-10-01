# Interoperability

SDK 3 uses wire version 3 exclusively. Version 2 frames are rejected; there is no automatic downgrade or short-session compatibility path.

## Required agreements

Peers must agree on MTU, extension semantics, authentication tag length, trusted connection/epoch and directional security parameters. Only reliable DATA consumes the reliable packet number. Authentication uses an independent 64-bit security sequence and a provider-defined secure algorithm.

## Evidence

`test/test_wire.cpp`, `test/test_security.cpp`, `test/test_network.cpp` and the integration tests check encodings, authentication input, canonical forwarded AAD and routed delivery. The test signing provider is deterministic test machinery, not a production cryptographic implementation. Cross-vendor crypto interoperability and board PHY validation remain application responsibilities.
