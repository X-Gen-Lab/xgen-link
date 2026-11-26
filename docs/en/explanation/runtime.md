# Bounded Receive and Runtime Work

`XGL_ENABLE_ADVANCED_RUNTIME=ON` enables resumable receive validation and
`xgl_step_bounded()`. It is an explicit build option and defaults to OFF for every
profile. An OFF build retains the original parser layout and returns
`XGL_ERR_UNSUPPORTED` from the bounded entry. Select the option consistently for
the library and its consumers; use the generated build configuration.

## What the Limits Count

The existing `xgl_work_budget_t` controls newly read bytes per PHY and the parser
timeout. `xgl_execution_limits_t` adds two independent limits:

| Field | Scope and meaning |
| --- | --- |
| `parser_candidates` | Validation opportunities per link in one step |
| `tx_packets` | Packet submission attempts across transport maintenance in one step |

Zero prohibits the corresponding work; `SIZE_MAX` removes that limit. Timers
still run. A parser token is charged when an incoming byte makes a fixed header,
extension block or complete frame ready for inspection. One inspection can
complete more than one validation stage. During recovery, each candidate
inspection consumes a token; restoring the earliest incomplete candidate after
the scan also consumes one. Noise scanning and advancing already consumed
storage do not require a token.

This bounds the number of validation attempts, not CRC bytes or processor
cycles. A single inspection can checksum a complete frame. PHY callbacks,
authentication providers, application callbacks, memory moves and scans still
take time. Receive-triggered ACKs and explicit application sends are outside the
maintenance TX limit. Measure whole-step worst-case execution time on the
product, including these costs.

Bounded maintenance rotates its starting peer and processes that peer's due
retries before advancing its message tail. Continuously ready messages cannot
consume retry turns through a separate global message-pump phase. Rotation
provides work opportunities; callback cost, driver backpressure and actual
service latency still require product validation.

## Continuation and Input Ownership

The bounded step returns its first processing error and independently writes
`result.pending`. Check both. `pending` means that executable local work remains
because a limit prevented completion; it does not mean that the driver might
have unread bytes. Retry with positive limits through the same serialized owner.
An application delivery that returned BUSY needs an application-capacity event,
not a tight retry loop.

For RX, the existing parser cache holds both the candidate being parsed and the
unprocessed tail of the last read. The PHY writes only into exposed free cache
space, at most once per poll. Every accepted byte is either processed or retained
before returning. A paused recovery never leaves unread bytes in a temporary
stack buffer, and does not allocate another MTU-sized buffer.

The private span ingress reports `consumed`, which may be less than the supplied
length. The caller must retain and later resubmit exactly the unconsumed suffix;
the parser owns a copy of the consumed prefix. Its input must not overlap parser
storage. Product integrations normally use the public step API and synchronous
PHY contract rather than this private function.

A decoded frame view borrows parser storage. Delivery finishes synchronously
before the parser compacts or overwrites that storage. Callbacks must copy data
they need afterwards and must not reenter, reset or destroy the instance. An
upper-layer rejection still consumes the complete wire-valid frame; its payload
is not interpreted as another candidate.

`rx_bytes=0` prevents new PHY reads while allowing retained parser work and
transport maintenance to advance. With ADVANCED_RUNTIME enabled, ordinary
`xgl_step()` drains the same retained state with unlimited candidate and
maintenance quotas. Switching between the two entries therefore does not discard
input; switching to the ordinary entry also removes these work limits.

## Recovery and Time

After rejection, recovery examines later magic positions, preferring the earliest
complete valid frame. If none is complete, it retains the earliest possible
partial frame. Budget exhaustion preserves the scan cursor and that partial
candidate across calls, so resumption does not restart the scan from the rejected
header. Rejected scan candidates do not repeatedly report the original CRC
failure.

Retained suffixes conservatively keep the original candidate start time. A newly
started candidate in an unread input batch uses that batch's read timestamp,
not the later time when the work resumes. Budget suspension never refreshes an
existing frame's absolute deadline. Parser expiry or reset clears both recovery
state and the retained input batch; this is an intentional discard boundary.

Use the same monotonic millisecond clock for every operation and keep intervals
below `2^31` milliseconds. Zero receive timeout disables expiry. Timeout
maintenance runs even with zero candidate and RX quotas. See
[low-power runtime](../guides/low-power.md) for policy and deadline queries.

## Polling and Throughput

For a due link, one PHY read can accept at most:

```text
B = min(rx_bytes, XGL_DATALINK_RX_CHUNK_SIZE, free_parser_cache_bytes)
```

If reads occur every `T` seconds, `B / T` is an ideal upper bound on ingress
throughput in bytes/second, not a guaranteed service rate. Recovery pauses,
partial reads, insufficient candidate quotas and occupied cache space reduce it.
With enough tokens, a frame of `L` bytes needs at least `ceil(L / B)` reads when
`B` is constant. An RX event does not bypass a configured PHY polling interval.
Only an actual PHY RX invocation advances that interval, including a driver
error. Resuming a paused parser without reading the PHY does not postpone the
next permitted read.

For example, a 32-byte chunk polled every 1 ms has a 32,000 byte/s read ceiling
before parser, scheduling and driver costs. Increasing `rx_bytes` above 32 does
not increase that build's chunk size. Size the driver ring for the actual burst
and longest owner-task service gap; validate the parser timeout against the time
needed to receive and inspect the largest permitted frame.

## DMA and Owner-Task Integration

DMA ownership remains in the board driver. A typical path is:

```text
DMA -> driver-owned circular buffer -> synchronous PHY RX copy
    -> parser cache -> protocol/application callbacks in the owner task
```

The DMA ISR records producer progress and wakes the owner; it never calls the
protocol API. The driver must handle wrap, DMA cache coherency, publication of
producer progress, and overwrite detection for its actual platform. PHY RX copies
only stable available bytes into the supplied buffer, returns the actual count,
and never retains that pointer. Parser storage can move immediately after RX
returns and is not a persistent DMA destination. See [porting](../guides/porting.md).

The following owner iteration illustrates control flow. Every `product_*`
function is a product-supplied placeholder, not an SDK API or a verified board
adapter:

```c
void product_link_iteration(xgl_handle_t handle) {
    const xgl_work_budget_t budget = {128U, 100U};
    const xgl_execution_limits_t limits = {4U, 2U};
    xgl_execution_result_t result;
    xgl_error_t error = xgl_step_bounded(
        handle, product_monotonic_ms(), &budget, &limits, &result);
    product_report_step_result(error);
    if (result.pending) {
        product_schedule_owner_again();
        return;
    }
    uint32_t delay_ms;
    bool timed = xgl_next_timeout_for_work(
        handle, product_monotonic_ms(), &budget, &delay_ms);
    product_wait_for_rx_capacity_or_timeout(timed, timed ? delay_ms : 0U);
}
```

Scheduling must give other product work an opportunity to run. Registering an
event and waiting must be race-safe, and capacity notifications must wake the
same owner. A zero configured quota with persistent pending work requires a
policy change or external event; repeatedly scheduling the same zero quota
cannot drain it.

## Validation Scope

Host regression covers all input split points, randomized chunks and quotas
(seed `5785420`), nested candidates, earliest-partial restoration, retained input
after source-buffer mutation, cache capacity, timeout/reset and switching to the
ordinary receive path. Both enabled and disabled parser configurations are
checked. These tests validate byte conservation and state-machine behavior;
they do not establish board throughput, DMA correctness or worst-case latency.

Product acceptance must additionally exercise sustained input, corruption bursts,
driver-ring overflow, long callback execution, DMA wrap and cache operations,
clock wrap, and fairness with several active links. Record the complete image's
Flash, RAM and stack measurements for the selected feature/profile combination.
