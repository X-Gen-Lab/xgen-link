# Design Document

## Overview

x_gen_link is a modern, production-ready embedded communication protocol stack designed for resource-constrained MCU environments. The design emphasizes:

- **Multi-instance architecture**: Support multiple independent protocol stacks
- **Zero-copy optimization**: Minimize memory bandwidth and CPU overhead
- **Compile-time configuration**: Eliminate unused code through Kconfig
- **Thread safety**: Optional mutex protection for RTOS environments
- **Bare-metal support**: Run without RTOS dependencies
- **Minimal footprint**: 32KB RAM, 50KB Flash for basic configuration
- **Industrial quality**: Comprehensive testing, documentation, and CI/CD

The protocol implements a three-layer architecture (Data Link, Network, Transport) with adaptive retransmission, flow control, and optional compression/encryption.

## Architecture

### Layer Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     Application Layer                        │
│                  (User Code / Examples)                      │
└─────────────────────────────────────────────────────────────┘
                            ▲ │
                            │ │ xgl_send() / rx_callback()
                            │ ▼
┌─────────────────────────────────────────────────────────────┐
│                    Transport Layer                           │
│  • Reliable transmission (ACK/NACK)                         │
│  • Adaptive timeout (RTT estimation)                        │
│  • Sequence number management                               │
│  • Flow control (sliding window)                            │
│  • Packet fragmentation/reassembly                          │
└─────────────────────────────────────────────────────────────┘
                            ▲ │
                            │ │
                            │ ▼
┌─────────────────────────────────────────────────────────────┐
│                     Network Layer                            │
│  • Routing table management                                 │
│  • Address resolution                                       │
│  • Packet forwarding                                        │
│  • Dynamic routing (optional)                               │
└─────────────────────────────────────────────────────────────┘
                            ▲ │
                            │ │
                            │ ▼
┌─────────────────────────────────────────────────────────────┐
│                   Data Link Layer                            │
│  • Frame encapsulation/parsing                              │
│  • CRC8 (header) + CRC16 (frame)                           │
│  • State machine parser                                     │
│  • Error detection and statistics                           │
└─────────────────────────────────────────────────────────────┘
                            ▲ │
                            │ │ Physical callbacks
                            │ ▼
┌─────────────────────────────────────────────────────────────┐
│                   Physical Layer                             │
│  • UART / SPI / I2C / CAN / Custom                          │
│  • User-provided callbacks                                  │
└─────────────────────────────────────────────────────────────┘
```

### Multi-Instance Design

```c
/**
 * \brief           Protocol instance handle (opaque pointer)
 */
typedef struct xgl_instance* xgl_handle_t;

/**
 * \brief           Create a new protocol instance
 * \param[in]       config: Configuration structure
 * \return          Instance handle, NULL on failure
 */
xgl_handle_t xgl_create(const xgl_config_t* config);

/**
 * \brief           Initialize protocol instance
 * \param[in]       handle: Instance handle
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_init(xgl_handle_t handle);

/**
 * \brief           Destroy protocol instance and free resources
 * \param[in]       handle: Instance handle
 */
void xgl_destroy(xgl_handle_t handle);
```

Each instance maintains its own:
- Memory pools (TX/RX buffers)
- Route table
- Sequence numbers
- Statistics
- Configuration
- Mutex (if thread-safe)

## Components and Interfaces

### 1. Configuration System

```c
/**
 * \brief           Memory allocator interface
 */
typedef struct {
    void* (*malloc)(size_t size);
    void (*free)(void* ptr);
    void* user_data;
} xgl_allocator_t;

/**
 * \brief           Physical layer interface
 */
typedef struct {
    xgl_error_t (*tx)(const uint8_t* data, size_t len);
    xgl_error_t (*rx)(uint8_t* buffer, size_t* len);
    void* user_data;
} xgl_phy_ops_t;

/**
 * \brief           Protocol configuration
 */
typedef struct {
    /* Instance identification */
    const char* name;
    uint8_t* source_id;
    size_t source_id_len;
    
    /* Memory configuration */
    size_t tx_pool_size;
    size_t rx_buffer_size;
    xgl_allocator_t* allocator;  /* NULL = use malloc/free */
    
    /* Protocol parameters */
    uint32_t ack_timeout_ms;
    uint8_t max_retry_count;
    uint8_t window_size;
    uint16_t max_frame_size;
    
    /* Routing */
    xgl_route_item_t* route_table;
    size_t route_table_len;
    
    /* Callbacks */
    xgl_rx_callback_t rx_callback;
    xgl_error_callback_t error_callback;
    void* callback_user_data;
    
    /* Features (compile-time + runtime) */
    bool enable_fragmentation;
    bool enable_compression;
    bool enable_encryption;
    bool thread_safe;
    
} xgl_config_t;

/**
 * \brief           Get default configuration
 * \param[out]      config: Configuration structure to fill
 */
void xgl_config_get_default(xgl_config_t* config);
```

### 2. Data Link Layer

#### Frame Format

```
┌────────┬─────────────────────────────────────────┬──────────┐
│  SOF   │           Frame Header (12 bytes)       │   CRC8   │
│ (0x55) │                                          │          │
└────────┴─────────────────────────────────────────┴──────────┘
         ┌─────────────────────────────────────────┬──────────┐
         │         Payload (0-N bytes)             │  CRC16   │
         │                                          │          │
         └─────────────────────────────────────────┴──────────┘

Frame Header (12 bytes):
┌──────────┬──────────┬───────────┬───────────┬──────────┬──────────┐
│ Version  │ DataType │ SourceID  │ TargetID  │  Attr    │  Attr    │
│ (4 bits) │ (4 bits) │ (1 byte)  │ (1 byte)  │  LSB     │  MSB     │
└──────────┴──────────┴───────────┴───────────┴──────────┴──────────┘
┌──────────┬──────────┬──────────┬──────────┬──────────┬──────────┐
│ DataLen  │ DataLen  │ SeqNum   │ AckNum   │ Reserved │  CRC8    │
│  LSB     │  MSB     │ (1 byte) │ (1 byte) │ (1 byte) │ (1 byte) │
└──────────┴──────────┴──────────┴──────────┴──────────┴──────────┘

Attributes LSB:
  [7:6] Reliable (00=None, 01=TX, 10=ACK)
  [5]   Fragment (0=No, 1=Yes)
  [4:3] Encrypt (00=None, 01=AES128, 10=ChaCha20)
  [2:0] Priority (0-7)

Attributes MSB:
  [7:6] Compress (00=None, 01=RLE, 10=LZ77, 11=ZLIB)
  [5:0] Reserved
```

#### Frame Parser State Machine

```c
typedef enum {
    XGL_PARSE_SOF,          /* Searching for SOF */
    XGL_PARSE_HEADER,       /* Receiving header */
    XGL_PARSE_PAYLOAD,      /* Receiving payload */
    XGL_PARSE_CRC,          /* Receiving CRC16 */
} xgl_parse_state_t;

typedef struct {
    xgl_parse_state_t state;
    uint8_t* cache;
    size_t cache_len;
    size_t index;
    uint32_t timestamp;
} xgl_parser_t;
```

#### Data Link Layer Interface

```c
/**
 * \brief           Send frame (internal)
 * \param[in]       handle: Instance handle
 * \param[in]       frame: Frame structure
 * \return          XGL_OK on success
 */
xgl_error_t xgl_datalink_send(xgl_handle_t handle, xgl_frame_t* frame);

/**
 * \brief           Receive and parse frames (called periodically or from ISR)
 * \param[in]       handle: Instance handle
 * \param[in]       freq_hz: Calling frequency in Hz
 */
void xgl_datalink_receive(xgl_handle_t handle, uint32_t freq_hz);
```

### 3. Network Layer

#### Routing Table

```c
/**
 * \brief           Route table entry
 */
typedef struct {
    uint8_t target_id;
    xgl_phy_ops_t* phy;
    uint16_t max_frame_size;
    uint32_t read_freq_hz;
    uint8_t metric;  /* For dynamic routing */
} xgl_route_item_t;

/**
 * \brief           Route lookup (O(1) with hash table)
 */
static inline xgl_phy_ops_t* xgl_route_find(xgl_handle_t handle, 
                                            uint8_t target_id);
```

#### Network Layer Interface

```c
/**
 * \brief           Send packet (with routing)
 * \param[in]       handle: Instance handle
 * \param[in]       packet: Packet structure
 * \param[in]       assign_seq: Assign sequence number
 * \return          XGL_OK on success
 */
xgl_error_t xgl_network_send(xgl_handle_t handle, 
                             xgl_packet_t* packet,
                             bool assign_seq);

/**
 * \brief           Receive packet (from data link layer)
 * \param[in]       handle: Instance handle
 * \param[in]       frame_buf: Frame buffer
 * \param[in]       frame_len: Frame length
 * \return          XGL_OK on success
 */
xgl_error_t xgl_network_receive(xgl_handle_t handle,
                                uint8_t* frame_buf,
                                size_t frame_len);
```

### 4. Transport Layer

#### Adaptive Retransmission

```c
/**
 * \brief           RTT estimator (RFC 6298)
 */
typedef struct {
    int32_t srtt;       /* Smoothed RTT */
    int32_t rttvar;     /* RTT variation */
    int32_t rto;        /* Retransmission timeout */
} xgl_rtt_estimator_t;

/**
 * \brief           Update RTT estimate
 * \param[in,out]   est: RTT estimator
 * \param[in]       measured_rtt: Measured RTT in ms
 */
static void xgl_rtt_update(xgl_rtt_estimator_t* est, int32_t measured_rtt) {
    if (est->srtt == 0) {
        /* First measurement */
        est->srtt = measured_rtt;
        est->rttvar = measured_rtt / 2;
    } else {
        /* RFC 6298 algorithm */
        int32_t err = measured_rtt - est->srtt;
        est->srtt += err / 8;
        est->rttvar += (abs(err) - est->rttvar) / 4;
    }
    est->rto = est->srtt + 4 * est->rttvar;
    
    /* Clamp RTO */
    if (est->rto < XGL_MIN_RTO_MS) est->rto = XGL_MIN_RTO_MS;
    if (est->rto > XGL_MAX_RTO_MS) est->rto = XGL_MAX_RTO_MS;
}
```

#### Sliding Window

```c
/**
 * \brief           Sliding window for flow control
 */
typedef struct {
    uint8_t window_size;
    uint8_t send_base;
    uint8_t next_seq_num;
    uint8_t expected_seq_num;
    bool* ack_received;  /* Bitmap */
} xgl_sliding_window_t;

/**
 * \brief           Check if window allows sending
 */
static inline bool xgl_window_can_send(xgl_sliding_window_t* win) {
    return (win->next_seq_num - win->send_base) < win->window_size;
}
```

#### Transport Layer Interface

```c
/**
 * \brief           Send data (public API)
 * \param[in]       handle: Instance handle
 * \param[in]       tx_data: Transmission data
 * \return          XGL_OK on success
 */
xgl_error_t xgl_send(xgl_handle_t handle, const xgl_tx_data_t* tx_data);

/**
 * \brief           Periodic transport layer processing
 * \param[in]       handle: Instance handle
 * \param[in]       freq_hz: Calling frequency in Hz
 */
void xgl_transport_run(xgl_handle_t handle, uint32_t freq_hz);
```

### 5. Memory Management

#### Tiered Memory Pool

```c
/**
 * \brief           Tiered memory pool for different packet sizes
 */
typedef struct {
    xgl_mempool_t* small_pool;   /* <= 64 bytes */
    xgl_mempool_t* medium_pool;  /* <= 256 bytes */
    xgl_mempool_t* large_pool;   /* <= 1024 bytes */
} xgl_tiered_pool_t;

/**
 * \brief           Smart allocation (selects appropriate pool)
 */
void* xgl_smart_alloc(xgl_tiered_pool_t* pool, size_t size);
```

#### Object Pool

```c
/**
 * \brief           Packet object pool
 */
typedef struct {
    xgl_packet_t* free_list;
    uint32_t total_count;
    uint32_t free_count;
} xgl_packet_pool_t;

/**
 * \brief           Allocate packet from pool
 */
xgl_packet_t* xgl_packet_alloc(xgl_packet_pool_t* pool);

/**
 * \brief           Return packet to pool
 */
void xgl_packet_free(xgl_packet_pool_t* pool, xgl_packet_t* packet);
```

### 6. Zero-Copy API

```c
/**
 * \brief           Zero-copy transmission data
 */
typedef struct {
    uint8_t* buffer;         /* Buffer with pre-allocated header space */
    size_t buffer_size;      /* Total buffer size */
    size_t data_offset;      /* Data start offset (= XGL_FRAME_HEADER_SIZE) */
    size_t data_len;         /* Actual data length */
    
    /* Transmission parameters */
    uint8_t target_id;
    uint8_t data_type;
    bool reliable;
    uint8_t priority;
} xgl_tx_data_zerocopy_t;

/**
 * \brief           Zero-copy send (no memory copy)
 * \param[in]       handle: Instance handle
 * \param[in]       tx_data: Zero-copy transmission data
 * \return          XGL_OK on success
 */
xgl_error_t xgl_send_zerocopy(xgl_handle_t handle, 
                              xgl_tx_data_zerocopy_t* tx_data);
```

## Data Models

### Core Data Structures

```c
/**
 * \brief           Packet data with reference counting
 */
typedef struct {
    atomic_uint ref_count;
    size_t data_len;
    uint8_t* data;
} xgl_packet_data_t;

/**
 * \brief           Protocol packet
 */
typedef struct {
    /* Addressing */
    uint8_t source_id;
    uint8_t target_id;
    uint8_t seq_num;
    uint8_t ack_num;
    
    /* Attributes */
    uint8_t version;
    uint8_t data_type;
    uint8_t reliable;
    uint8_t fragment;
    uint8_t encrypt;
    uint8_t priority;
    uint8_t compress;
    
    /* Data */
    xgl_packet_data_t* data;
    
    /* Retransmission */
    uint8_t retry_count;
    int32_t wait_time_ms;
    uint32_t send_timestamp;
    
    /* Routing */
    xgl_phy_ops_t* phy;
    
    /* List node */
    xgl_list_node_t node;
} xgl_packet_t;

/**
 * \brief           Protocol instance (internal)
 */
typedef struct xgl_instance {
    /* Configuration */
    xgl_config_t config;
    bool initialized;
    
    /* Memory management */
    xgl_allocator_t allocator;
    xgl_tiered_pool_t tx_pool;
    xgl_packet_pool_t packet_pool;
    
    /* Routing */
    xgl_route_hashtable_t route_table;
    
    /* Sequence numbers (per route) */
    uint8_t* seq_numbers;
    
    /* Transport layer */
    xgl_list_t wait_ack_list;
    xgl_sliding_window_t* windows;  /* Per route */
    xgl_rtt_estimator_t* rtt_est;   /* Per route */
    
    /* Data link layer */
    xgl_list_t rx_parser_list;
    uint8_t* rx_buffer;
    size_t rx_buffer_size;
    
    /* Statistics */
    xgl_statistics_t stats;
    
    /* Thread safety */
#ifdef XGL_THREAD_SAFE
    xgl_mutex_t mutex;
#endif
    
} xgl_instance_t;
```

### Statistics Structure

```c
/**
 * \brief           Protocol statistics
 */
typedef struct {
    /* Transmission */
    uint64_t tx_packets;
    uint64_t tx_bytes;
    uint64_t tx_errors;
    uint64_t tx_retries;
    
    /* Reception */
    uint64_t rx_packets;
    uint64_t rx_bytes;
    uint64_t rx_errors;
    uint64_t rx_crc8_errors;
    uint64_t rx_crc16_errors;
    uint64_t rx_dropped;
    
    /* Performance */
    uint32_t avg_rtt_ms;
    uint32_t max_rtt_ms;
    uint32_t min_rtt_ms;
    
    /* Memory */
    size_t memory_used;
    size_t memory_peak;
    
} xgl_statistics_t;
```

## Correctness Properties

*A property is a characteristic or behavior that should hold true across all valid executions of a system—essentially, a formal statement about what the system should do. Properties serve as the bridge between human-readable specifications and machine-verifiable correctness guarantees.*


### Property Reflection

After analyzing all acceptance criteria, I identified the following testable properties. Some properties were combined to eliminate redundancy:

- Properties 1.3 and 2.3 both test memory leak prevention → Combined into Property 1
- Properties 3.3 and 3.4 both test CRC error handling → Combined into Property 2
- Properties 12.1 and 51.2 both test byte order → Combined into Property 3
- Properties 13.1 and 13.2 both test CRC calculation → Combined into Property 4

### Correctness Properties

**Property 1: Memory Leak Prevention**

*For any* protocol instance, creating and then destroying it should release all allocated memory without leaks.

**Validates: Requirements 1.3, 2.3**

**Property 2: CRC Error Detection**

*For any* frame with corrupted CRC (either CRC8 or CRC16), the protocol should reject it and increment the appropriate error counter.

**Validates: Requirements 3.3, 3.4, 13.4**

**Property 3: Serialization Round-Trip**

*For any* multi-byte value, serializing it to little-endian byte order and then deserializing should produce the original value.

**Validates: Requirements 12.1, 12.2, 51.2, 51.3**

**Property 4: CRC Calculation Correctness**

*For any* data buffer, the CRC8 (MAXIM) and CRC16 (MODBUS) calculations should match reference implementations.

**Validates: Requirements 13.1, 13.2, 13.3**

**Property 5: Frame Encapsulation Round-Trip**

*For any* valid packet data, encapsulating it into a frame and then parsing the frame should produce equivalent packet data.

**Validates: Requirements 3.1, 3.2, 3.5**

**Property 6: Instance Isolation**

*For any* two protocol instances, modifying the state of one instance should not affect the state of the other instance.

**Validates: Requirements 1.4**

**Property 7: Custom Allocator Usage**

*For any* protocol instance created with a custom allocator, all memory operations should go through that allocator.

**Validates: Requirements 2.1**

**Property 8: Allocation Failure Handling**

*For any* allocation failure during initialization, the protocol should return an error and clean up all partial allocations.

**Validates: Requirements 2.2, 8.3**

**Property 9: Memory Pool Exhaustion**

*For any* protocol instance, when the memory pool is exhausted, send operations should return an error without corrupting state.

**Validates: Requirements 2.5**

**Property 10: Route Lookup Correctness**

*For any* target ID in the route table, the network layer should find the correct PHY interface.

**Validates: Requirements 4.1, 4.2**

**Property 11: Route Not Found Handling**

*For any* target ID not in the route table, the network layer should return an error and invoke the error callback.

**Validates: Requirements 4.3**

**Property 12: Packet Forwarding to Self**

*For any* packet addressed to the local node's ID, the network layer should forward it to the transport layer.

**Validates: Requirements 4.4**

**Property 13: Reliable Transmission Queuing**

*For any* packet sent with reliable transmission enabled, the transport layer should add it to the wait-ACK queue.

**Validates: Requirements 5.1**

**Property 14: Retransmission on Timeout**

*For any* packet in the wait-ACK queue, if no ACK is received within the timeout period, the packet should be retransmitted.

**Validates: Requirements 5.2**

**Property 15: Retry Exhaustion Handling**

*For any* packet that exceeds maximum retry count, the transport layer should invoke the error callback and remove the packet from the queue.

**Validates: Requirements 5.3**

**Property 16: ACK Processing**

*For any* received ACK with matching sequence number and target ID, the transport layer should remove the corresponding packet from the wait-ACK queue.

**Validates: Requirements 5.4**

**Property 17: ACK Generation**

*For any* received reliable packet, the transport layer should send an ACK containing the packet's sequence number.

**Validates: Requirements 5.5**

**Property 18: RTT Estimation**

*For any* received ACK, the transport layer should update the RTT estimate using exponential moving average (SRTT += error/8, RTTVAR += (|error| - RTTVAR)/4).

**Validates: Requirements 6.1**

**Property 19: RTO Calculation**

*For any* RTT estimate, the calculated RTO should equal SRTT + 4 * RTTVAR, clamped to [MIN_RTO, MAX_RTO].

**Validates: Requirements 6.2**

**Property 20: Exponential Backoff**

*For any* packet that is retransmitted multiple times, the timeout should increase exponentially with each retry.

**Validates: Requirements 6.4**

**Property 21: Sequence Number Monotonicity**

*For any* sequence of sent packets to the same target, the sequence numbers should be monotonically increasing (modulo 256).

**Validates: Requirements 7.1**

**Property 22: Duplicate Packet Handling**

*For any* duplicate packet (same sequence number), the transport layer should discard it and send an ACK.

**Validates: Requirements 7.4**

**Property 23: Sliding Window Maintenance**

*For any* protocol instance, the sliding window state (send_base, next_seq_num) should satisfy: 0 <= (next_seq_num - send_base) <= window_size.

**Validates: Requirements 7.5**

**Property 24: Error Code Specificity**

*For any* error condition, the protocol should return a specific error code that identifies the failure type.

**Validates: Requirements 8.1**

**Property 25: Error Callback Invocation**

*For any* error when an error callback is registered, the protocol should invoke the callback with correct error details.

**Validates: Requirements 8.2, 8.4**

**Property 26: Error Statistics**

*For any* error occurrence, the protocol should increment the appropriate error counter in statistics.

**Validates: Requirements 8.5**

**Property 27: Field Validation**

*For any* received frame, the protocol should validate all field values (version, data_type, etc.) before processing.

**Validates: Requirements 12.3**

**Property 28: Alignment Safety**

*For any* multi-byte field access, the protocol should use byte-wise access on strict-alignment platforms (ARM Cortex-M0).

**Validates: Requirements 12.5, 52.2**

**Property 29: Fragmentation Correctness**

*For any* data exceeding max frame size, fragmenting and then reassembling should produce the original data.

**Validates: Requirements 14.1, 14.2, 14.3, 14.4**

**Property 30: Fragment Timeout Handling**

*For any* incomplete fragment set, if the reassembly timeout expires, the protocol should discard partial data and report an error.

**Validates: Requirements 14.5**

## Error Handling

### Error Codes

```c
/**
 * \brief           Protocol error codes
 */
typedef enum {
    XGL_OK = 0,                     /* Success */
    
    /* Parameter errors (1-99) */
    XGL_ERR_INVALID_PARAM = 1,      /* Invalid parameter */
    XGL_ERR_NULL_POINTER = 2,       /* Null pointer */
    XGL_ERR_NOT_INITIALIZED = 3,    /* Not initialized */
    XGL_ERR_ALREADY_INITIALIZED = 4,/* Already initialized */
    
    /* Memory errors (100-199) */
    XGL_ERR_NO_MEMORY = 100,        /* Out of memory */
    XGL_ERR_POOL_EXHAUSTED = 101,   /* Memory pool exhausted */
    XGL_ERR_BUFFER_TOO_SMALL = 102, /* Buffer too small */
    
    /* Network errors (200-299) */
    XGL_ERR_ROUTE_NOT_FOUND = 200,  /* Route not found */
    XGL_ERR_TX_FAILED = 201,        /* Transmission failed */
    XGL_ERR_TIMEOUT = 202,          /* Operation timeout */
    XGL_ERR_ACK_TIMEOUT = 203,      /* ACK timeout */
    
    /* Protocol errors (300-399) */
    XGL_ERR_INVALID_FRAME = 300,    /* Invalid frame */
    XGL_ERR_CRC_FAILED = 301,       /* CRC check failed */
    XGL_ERR_INVALID_VERSION = 302,  /* Invalid version */
    XGL_ERR_INVALID_DATA_TYPE = 303,/* Invalid data type */
    XGL_ERR_SEQUENCE_ERROR = 304,   /* Sequence number error */
    
    /* State errors (400-499) */
    XGL_ERR_BUSY = 400,             /* Resource busy */
    XGL_ERR_QUEUE_FULL = 401,       /* Queue full */
    XGL_ERR_WINDOW_FULL = 402,      /* Sliding window full */
    
} xgl_error_t;

/**
 * \brief           Get error string
 * \param[in]       error: Error code
 * \return          Error description string
 */
const char* xgl_error_string(xgl_error_t error);
```

### Error Callback

```c
/**
 * \brief           Error callback function type
 * \param[in]       handle: Instance handle
 * \param[in]       error: Error code
 * \param[in]       message: Error message
 * \param[in]       user_data: User data
 */
typedef void (*xgl_error_callback_t)(xgl_handle_t handle,
                                     xgl_error_t error,
                                     const char* message,
                                     void* user_data);
```

### Error Handling Strategy

1. **Return error codes**: All functions return `xgl_error_t`
2. **Invoke callbacks**: Critical errors invoke registered error callback
3. **Update statistics**: All errors increment appropriate counters
4. **Clean up resources**: Failed operations clean up partial state
5. **Log errors**: Errors are logged if logging is enabled

## Testing Strategy

### Dual Testing Approach

The protocol uses both unit tests and property-based tests:

**Unit Tests**:
- Specific examples demonstrating correct behavior
- Edge cases (sequence number wraparound, buffer boundaries)
- Error conditions (allocation failures, invalid inputs)
- Integration between layers

**Property-Based Tests**:
- Universal properties across all inputs (30 properties defined above)
- Minimum 100 iterations per property test
- Each test tagged with: `Feature: x-gen-link, Property N: <description>`
- Uses Hypothesis (Python) or QuickCheck (C) framework

### Test Configuration

```c
/* Property test configuration */
#define XGL_PROPERTY_TEST_ITERATIONS 100
#define XGL_PROPERTY_TEST_TIMEOUT_MS 5000

/* Test tags format */
// Feature: x-gen-link, Property 1: Memory Leak Prevention
// Feature: x-gen-link, Property 5: Frame Encapsulation Round-Trip
```

### Coverage Goals

- **Line coverage**: > 80% for all modules
- **Branch coverage**: > 75% for critical paths
- **Property coverage**: All 30 properties tested
- **Platform coverage**: Native, ARM Cortex-M0, ARM Cortex-M4

### Test Execution

```bash
# Run all tests
python scripts/test/test.py

# Run property tests only
python scripts/test/test.py -l property

# Run with coverage
python scripts/test/test.py --coverage

# Run specific property
python scripts/test/test.py -f "Property 5*"
```

## Platform Abstraction

### Thread Safety Abstraction

```c
#ifdef XGL_THREAD_SAFE

/**
 * \brief           Mutex type (platform-specific)
 */
typedef struct xgl_mutex xgl_mutex_t;

/**
 * \brief           Initialize mutex
 */
xgl_error_t xgl_mutex_init(xgl_mutex_t* mutex);

/**
 * \brief           Lock mutex
 */
xgl_error_t xgl_mutex_lock(xgl_mutex_t* mutex);

/**
 * \brief           Unlock mutex
 */
xgl_error_t xgl_mutex_unlock(xgl_mutex_t* mutex);

/**
 * \brief           Destroy mutex
 */
void xgl_mutex_destroy(xgl_mutex_t* mutex);

#else

/* No-op macros for bare-metal */
#define xgl_mutex_init(m) XGL_OK
#define xgl_mutex_lock(m) XGL_OK
#define xgl_mutex_unlock(m) XGL_OK
#define xgl_mutex_destroy(m) ((void)0)

#endif
```

### Time Abstraction

```c
/**
 * \brief           Get current time in milliseconds
 * \return          Current time in ms
 */
uint32_t xgl_time_ms(void);

/**
 * \brief           Delay for specified milliseconds
 * \param[in]       ms: Delay time in milliseconds
 */
void xgl_delay_ms(uint32_t ms);
```

### Atomic Operations

```c
#ifdef XGL_THREAD_SAFE
#include <stdatomic.h>
typedef atomic_uint xgl_atomic_t;
#define xgl_atomic_inc(ptr) atomic_fetch_add(ptr, 1)
#define xgl_atomic_dec(ptr) atomic_fetch_sub(ptr, 1)
#define xgl_atomic_load(ptr) atomic_load(ptr)
#define xgl_atomic_store(ptr, val) atomic_store(ptr, val)
#else
typedef uint32_t xgl_atomic_t;
#define xgl_atomic_inc(ptr) (++(*(ptr)))
#define xgl_atomic_dec(ptr) (--(*(ptr)))
#define xgl_atomic_load(ptr) (*(ptr))
#define xgl_atomic_store(ptr, val) (*(ptr) = (val))
#endif
```

## Compile-Time Configuration

### Kconfig Options

```kconfig
menu "x_gen_link Protocol Configuration"

config XGL_ENABLE
    bool "Enable x_gen_link Protocol"
    default y
    help
      Enable the x_gen_link communication protocol stack.

config XGL_MAX_INSTANCES
    int "Maximum protocol instances"
    default 4
    range 1 16
    help
      Maximum number of protocol instances that can be created.

config XGL_DEFAULT_TX_POOL_SIZE
    int "Default TX memory pool size (bytes)"
    default 4096
    range 1024 65536
    help
      Default size of the transmission memory pool.

config XGL_DEFAULT_RX_BUFFER_SIZE
    int "Default RX buffer size (bytes)"
    default 512
    range 128 4096
    help
      Default size of the reception buffer.

config XGL_THREAD_SAFE
    bool "Enable thread safety"
    default n
    help
      Enable mutex protection for multi-threaded environments.

config XGL_ENABLE_FRAGMENTATION
    bool "Enable packet fragmentation"
    default y
    help
      Enable support for fragmenting large packets.

config XGL_ENABLE_COMPRESSION
    bool "Enable data compression"
    default n
    help
      Enable optional data compression (RLE/LZ77/ZLIB).

config XGL_ENABLE_ENCRYPTION
    bool "Enable data encryption"
    default n
    help
      Enable optional data encryption (AES/ChaCha20).

config XGL_ENABLE_STATISTICS
    bool "Enable statistics collection"
    default y
    help
      Enable collection of protocol statistics.

config XGL_ENABLE_LOGGING
    bool "Enable logging"
    default y
    help
      Enable protocol logging for debugging.

config XGL_LOG_LEVEL
    int "Log level (0=None, 1=Error, 2=Warning, 3=Info, 4=Debug)"
    default 2
    range 0 4
    depends on XGL_ENABLE_LOGGING
    help
      Set the logging level.

endmenu
```

### Configuration Presets

```c
/**
 * \brief           Tiny configuration (32KB RAM, 50KB Flash)
 */
#define XGL_CONFIG_PRESET_TINY { \
    .tx_pool_size = 1024, \
    .rx_buffer_size = 128, \
    .max_retry_count = 3, \
    .window_size = 2, \
    .max_frame_size = 128, \
    .enable_fragmentation = false, \
    .enable_compression = false, \
    .enable_encryption = false, \
}

/**
 * \brief           Small configuration (64KB RAM, 100KB Flash)
 */
#define XGL_CONFIG_PRESET_SMALL { \
    .tx_pool_size = 2048, \
    .rx_buffer_size = 256, \
    .max_retry_count = 5, \
    .window_size = 4, \
    .max_frame_size = 256, \
    .enable_fragmentation = true, \
    .enable_compression = false, \
    .enable_encryption = false, \
}

/**
 * \brief           Medium configuration (128KB RAM, 256KB Flash)
 */
#define XGL_CONFIG_PRESET_MEDIUM { \
    .tx_pool_size = 4096, \
    .rx_buffer_size = 512, \
    .max_retry_count = 5, \
    .window_size = 8, \
    .max_frame_size = 512, \
    .enable_fragmentation = true, \
    .enable_compression = true, \
    .enable_encryption = false, \
}

/**
 * \brief           Large configuration (256KB+ RAM, 512KB+ Flash)
 */
#define XGL_CONFIG_PRESET_LARGE { \
    .tx_pool_size = 8192, \
    .rx_buffer_size = 1024, \
    .max_retry_count = 7, \
    .window_size = 16, \
    .max_frame_size = 1024, \
    .enable_fragmentation = true, \
    .enable_compression = true, \
    .enable_encryption = true, \
}
```

## Performance Considerations

### Memory Footprint

| Configuration | RAM Usage | Flash Usage | Features |
|---------------|-----------|-------------|----------|
| Tiny | ~8KB | ~30KB | Basic TX/RX, no fragmentation |
| Small | ~16KB | ~50KB | + Fragmentation |
| Medium | ~32KB | ~80KB | + Compression |
| Large | ~64KB | ~120KB | + Encryption, full features |

### CPU Usage

- **Frame parsing**: ~50 CPU cycles per byte (state machine)
- **CRC calculation**: ~10 CPU cycles per byte (lookup table)
- **Zero-copy send**: ~200 CPU cycles (no memcpy)
- **Standard send**: ~500 CPU cycles + memcpy overhead
- **ACK processing**: ~1000 CPU cycles

### Throughput

- **UART @ 115200 bps**: ~10 KB/s effective (with overhead)
- **SPI @ 1 MHz**: ~100 KB/s effective
- **CAN @ 500 kbps**: ~50 KB/s effective

### Latency

- **Minimum latency**: ~5ms (send + ACK)
- **Typical latency**: ~20ms (with retransmission)
- **Maximum latency**: ~5s (max retries exhausted)

## Documentation Requirements

### API Documentation (Doxygen)

All public functions must have:
- `\brief`: One-line description
- `\param[in]`, `\param[out]`, `\param[in,out]`: Parameter descriptions
- `\return`: Return value description
- `\note`: Additional notes (optional)
- `\warning`: Warnings (optional)

### User Guide

- Quick start guide
- Configuration guide
- Integration guide
- Porting guide
- Troubleshooting guide

### Architecture Documentation

- Layer responsibilities
- Data flow diagrams
- State machine diagrams
- Sequence diagrams

### Example Applications

1. **Echo Server**: Simple request-response
2. **File Transfer**: Reliable large data transfer
3. **Multi-Node Network**: Routing and forwarding
4. **Bare-Metal Integration**: No RTOS example
5. **FreeRTOS Integration**: Thread-safe example

## Build System Integration

### CMake Integration

```cmake
# Add x_gen_link to your project
add_subdirectory(x_gen_link)

# Link against x_gen_link
target_link_libraries(your_app PRIVATE xgl)

# Configure options
option(XGL_THREAD_SAFE "Enable thread safety" OFF)
option(XGL_ENABLE_COMPRESSION "Enable compression" OFF)
option(XGL_ENABLE_ENCRYPTION "Enable encryption" OFF)
```

### Kconfig Integration

```kconfig
# In your project's Kconfig
source "x_gen_link/Kconfig"
```

### Single-Header Integration

```c
/* For simple projects, use single-header mode */
#define XGL_IMPLEMENTATION
#include "xgl_single_header.h"
```

## Conclusion

This design provides a comprehensive, production-ready embedded communication protocol stack optimized for resource-constrained MCUs. Key design decisions:

1. **Multi-instance architecture** eliminates global state
2. **Zero-copy API** minimizes memory bandwidth
3. **Compile-time configuration** reduces code size
4. **Property-based testing** ensures correctness
5. **Platform abstraction** enables portability
6. **Comprehensive documentation** aids adoption

The design addresses all 55 requirements and provides 30 testable correctness properties for validation.


## Testing Strategy

### Test Framework

The protocol uses **Google Test (gtest)** and **Google Mock (gmock)** for all testing:

- **Google Test**: C++ testing framework for unit tests and property-based tests
- **Google Mock**: Mocking framework for isolating components and testing interactions
- **Integration**: CMake-based integration with FetchContent or find_package

### Dual Testing Approach

**Unit Tests** (Google Test):
- Specific examples demonstrating correct behavior
- Edge cases (sequence number wraparound, buffer boundaries)
- Error conditions (allocation failures, invalid inputs)
- Integration between layers
- Test fixtures for setup/teardown
- Parameterized tests for multiple input combinations

**Property-Based Tests** (Google Test + Custom Generators):
- Universal properties across all inputs (30 properties defined above)
- Minimum 100 iterations per property test
- Each test tagged with: `Feature: x-gen-link, Property N: <description>`
- Custom random input generators for protocol structures
- Shrinking support for minimal failing examples

### Mock Objects (Google Mock)

**Physical Layer Mock**:
```cpp
class MockPhyOps {
public:
    MOCK_METHOD(xgl_error_t, tx, (const uint8_t* data, size_t len));
    MOCK_METHOD(xgl_error_t, rx, (uint8_t* buffer, size_t* len));
};
```

**Allocator Mock**:
```cpp
class MockAllocator {
public:
    MOCK_METHOD(void*, malloc, (size_t size));
    MOCK_METHOD(void, free, (void* ptr));
};
```

**Callback Mocks**:
```cpp
class MockCallbacks {
public:
    MOCK_METHOD(void, rx_callback, 
                (xgl_handle_t handle, const uint8_t* data, size_t len));
    MOCK_METHOD(void, error_callback,
                (xgl_handle_t handle, xgl_error_t error, const char* msg));
};
```

### Test Configuration

```cpp
/* Property test configuration */
#define XGL_PROPERTY_TEST_ITERATIONS 100
#define XGL_PROPERTY_TEST_TIMEOUT_MS 5000

/* Test tags using Google Test */
// Feature: x-gen-link, Property 1: Memory Leak Prevention
TEST(XglMemoryTest, PropertyMemoryLeakPrevention) {
    // Test implementation with 100+ iterations
    for (int i = 0; i < 100; ++i) {
        // Create and destroy instance
        // Verify no memory leaks
    }
}

// Feature: x-gen-link, Property 5: Frame Encapsulation Round-Trip
TEST(XglFrameTest, PropertyFrameEncapsulationRoundTrip) {
    // Test implementation with random inputs
    for (int i = 0; i < 100; ++i) {
        // Generate random packet
        // Encapsulate to frame
        // Parse frame back
        // Verify equivalence
    }
}
```

### Test Structure

```
test/
├── unit/                      # Unit tests
│   ├── test_crc.cpp          # CRC calculation tests
│   ├── test_serialize.cpp    # Serialization tests
│   ├── test_mempool.cpp      # Memory pool tests
│   ├── test_datalink.cpp     # Data link layer tests
│   ├── test_network.cpp      # Network layer tests
│   └── test_transport.cpp    # Transport layer tests
├── property/                  # Property-based tests
│   ├── property_framework.h  # Property test utilities
│   ├── test_property_memory.cpp
│   ├── test_property_crc.cpp
│   ├── test_property_frame.cpp
│   ├── test_property_network.cpp
│   └── test_property_transport.cpp
├── integration/               # Integration tests
│   └── test_integration.cpp
├── mocks/                     # Mock objects
│   ├── mock_phy.h
│   ├── mock_allocator.h
│   └── mock_callbacks.h
└── CMakeLists.txt            # Test build configuration
```

### Coverage Goals

- **Line coverage**: > 80% for all modules
- **Branch coverage**: > 75% for critical paths
- **Property coverage**: All 30 properties tested
- **Platform coverage**: Native, ARM Cortex-M0, ARM Cortex-M4

### Test Execution

```bash
# Build tests
cmake --build build --target xgl_tests

# Run all tests
./build/test/xgl_tests

# Run specific test suite
./build/test/xgl_tests --gtest_filter=XglMemoryTest.*

# Run with coverage
cmake -DCMAKE_BUILD_TYPE=Coverage ..
make coverage

# Run with memory leak detection
valgrind --leak-check=full ./build/test/xgl_tests
```

### CMake Test Integration

```cmake
# In test/CMakeLists.txt
include(FetchContent)

# Fetch Google Test
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

# Enable testing
enable_testing()
include(GoogleTest)

# Add test executable
add_executable(xgl_tests
    unit/test_crc.cpp
    unit/test_serialize.cpp
    unit/test_mempool.cpp
    unit/test_datalink.cpp
    unit/test_network.cpp
    unit/test_transport.cpp
    property/test_property_memory.cpp
    property/test_property_crc.cpp
    property/test_property_frame.cpp
    property/test_property_network.cpp
    property/test_property_transport.cpp
    integration/test_integration.cpp
)

target_link_libraries(xgl_tests
    PRIVATE
    xgl
    GTest::gtest_main
    GTest::gmock
)

# Discover tests
gtest_discover_tests(xgl_tests)
```

### Property Test Example

```cpp
// test/property/test_property_memory.cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "xgl/xgl.h"
#include "test_utils.h"

// Feature: x-gen-link, Property 1: Memory Leak Prevention
TEST(XglPropertyMemory, MemoryLeakPrevention) {
    for (int i = 0; i < 100; ++i) {
        // Generate random configuration
        xgl_config_t config = generate_random_config();
        
        // Create instance
        xgl_handle_t handle = xgl_create(&config);
        ASSERT_NE(handle, nullptr);
        
        // Initialize
        ASSERT_EQ(xgl_init(handle), XGL_OK);
        
        // Destroy
        xgl_destroy(handle);
        
        // Verify no memory leaks (using memory tracking)
        ASSERT_EQ(get_allocated_bytes(), 0);
    }
}

// Feature: x-gen-link, Property 7: Custom Allocator Usage
TEST(XglPropertyMemory, CustomAllocatorUsage) {
    MockAllocator mock_alloc;
    
    // Expect all allocations go through custom allocator
    EXPECT_CALL(mock_alloc, malloc(testing::_))
        .Times(testing::AtLeast(1))
        .WillRepeatedly(testing::Invoke(::malloc));
    
    EXPECT_CALL(mock_alloc, free(testing::_))
        .Times(testing::AtLeast(1))
        .WillRepeatedly(testing::Invoke(::free));
    
    xgl_config_t config;
    xgl_config_get_default(&config);
    config.allocator = &mock_alloc;
    
    xgl_handle_t handle = xgl_create(&config);
    ASSERT_NE(handle, nullptr);
    
    xgl_init(handle);
    xgl_destroy(handle);
}
```
