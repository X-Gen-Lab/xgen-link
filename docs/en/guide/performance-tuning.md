# Performance Tuning

Measure the selected profile and real driver before changing capacities. Host throughput and structure sizes do not predict a small MCU's Flash, RAM, or worst-case latency.

## Window and Packet Capacity

A larger peer window can hide ACK latency, but the global `max_tx_packets` also limits total in-flight work. Increase them together only when measured traffic needs the capacity. Reserve out-of-order RX slots for windows above one.

Reliable message pumping allows large fragmented messages with a small packet window. It still needs a retained maximum-message slot per peer and explicit TX-message and RX-reassembly byte admission budgets.

## Frame and Memory Size

Larger frames reduce framing overhead but increase parser caches, scratch, reliable payload slots, and out-of-order payload slots. Query `xgl_memory_requirements()` for every candidate configuration.

Pools are partitioned by resource type and reserve maximum slot sizes. Lower byte admission budgets alone may not reduce workspace. Reduce peer counts, packet counts, frame size, message size, or reassembly slots to reduce the corresponding reservations.

Boot removes authentication, forwarding, fragmentation, out-of-order state, and route indexing. Use the real Cortex-M0 consumer to compare Flash and RAM; do not infer final image size from a static archive.

## Scheduling and Copies

Tune route polling frequency against latency and wakeups. RX work is budgeted per link, while callbacks and transport maintenance still contribute to step duration.

Use `xgl_send_zerocopy_at()` only for supported single-frame unreliable traffic. Reliable retransmission and fragmented-message ownership require retained copies. A synchronous PHY adapter may also need a driver-owned copy for DMA.

## Measurement and Diagnosis

Record application latency, PHY busy frequency, retransmission/error counters, and peak outstanding traffic. Keep stats sampling serialized. Use map files, final ELF symbols, compiler stack reports, and target stack high-water measurements.

A single-function `.su` maximum is not a call-chain or interrupt-stack bound. The Boot measurement includes a consumer and a generic startup, and excludes your BSP and application buffers. The smaller Boot design target remains an optimization target until the measured configuration meets it.
