# Glossary

| Term | Meaning |
| --- | --- |
| Peer scope | `(remote node, connection_id, session_epoch)`; owns reliability state |
| Reliable packet number | Sequence consumed only by reliable DATA |
| Security sequence | Independent 64-bit sequence consumed per signing attempt |
| Session | Explicit trusted directional security association |
| Workspace | Caller or backend reserved storage for instance and bounded pools |
| Profile | Compile-time capability and structure layout |
| PHY | Synchronous frame consumption and bounded stream reads |
| Backpressure | Rejection before acceptance when storage or application is busy |
| Tombstone | Closed security slot retained to prevent nonce-domain reuse |
