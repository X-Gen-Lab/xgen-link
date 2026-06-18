# Errors

| Range | Example | Meaning |
| --- | --- | --- |
| 0 | `XGL_OK` | Success |
| 1-99 | `XGL_ERR_INVALID_PARAM` | Parameter error |
| 100-199 | `XGL_ERR_NO_MEMORY` | Memory or buffer error |
| 200-299 | `XGL_ERR_ROUTE_NOT_FOUND` | Network and transport error |
| 300-399 | `XGL_ERR_INVALID_FRAME` | Protocol frame error |
| 400-499 | `XGL_ERR_QUEUE_FULL` | State or queue error |

Use `xgl_error_string()` for log text.

## Error Propagation Strategy

### Per-Layer Error Handling Rules

| Layer | Error type | Strategy | Description |
| --- | --- | --- | --- |
| Wire | CRC error | fail-closed | Drop frame, do not deliver upward |
| Wire | Invalid header field | fail-closed | Drop frame |
| Datalink | Authentication failure | fail-closed | Drop frame, report via error_callback |
| Datalink | Replay rejection | fail-closed | Drop frame, count |
| Network | Route not found | fail-closed | Drop packet, report error |
| Network | TTL expired | fail-closed | Drop packet |
| Network | MTU exceeded | fail-closed | Drop packet |
| Transport | Reliable queue full | drop | New packet is dropped |
| Transport | Peer state not found | create | Auto-create on first sight of new peer |
| Transport | Retransmission timeout | retry | retry_count++ until max_retry_count |
| Transport | max_retry exceeded | fail-closed | Report via error_callback |
| Memory | Allocation failure | return NULL | Upper layer decides degradation or rejection |

### Error Callback Thread Safety

- `xgl_error_callback_t` is called in `xgl_run()` context.
- In `XGL_THREAD_SAFE` mode, error_callback may be called from any `xgl_run()` thread.
- The callback must not perform long blocking operations.
- The callback must not call `xgl_send()` (potential deadlock).

### Error Recovery Paths

```text
Parameter error → return xgl_error_t, no state change
Frame parse error → drop frame, parser continues
Authentication failure → drop frame, error_callback
Retransmission limit exceeded → error_callback, peer state marked inactive
Memory exhausted → return NULL, upper layer decides
```

### Observability

All fail-closed paths should expose counters through `xgl_statistics_t`. During production debugging, check these first:

1. `rx_header_crc_errors` / `rx_crc16_errors`: physical layer quality issues
2. Authentication failure count: key misconfiguration or attack
3. Replay rejection count: network loops or retransmission anomalies
4. Reliable retransmission count: link quality