# Multi-Node Network Example

## Overview

This example demonstrates a three-node network with routing and packet forwarding capabilities using the xgen-link protocol stack. It showcases how to build multi-hop networks where intermediate nodes forward packets between source and destination.

## What This Example Demonstrates

1. **Multiple Protocol Instances**: Running three independent protocol instances simultaneously
2. **Network Topology**: Three-node linear topology (Node 1 ↔ Node 2 ↔ Node 3)
3. **Packet Routing**: Routing packets through intermediate nodes
4. **Packet Forwarding**: Node 2 acts as a router, forwarding packets between Node 1 and Node 3
5. **Multi-Hop Communication**: End-to-end communication across multiple hops
6. **Route Tables**: Configuring route tables for each node
7. **Bidirectional Communication**: Messages and replies flowing through the network
8. **Statistics Monitoring**: Per-node statistics to analyze network behavior

## Network Topology

```
┌─────────┐         ┌─────────┐         ┌─────────┐
│ Node 1  │ <-----> │ Node 2  │ <-----> │ Node 3  │
│ (ID=1)  │         │ (ID=2)  │         │ (ID=3)  │
│ Source  │         │ Router  │         │  Dest   │
└─────────┘         └─────────┘         └─────────┘
```

### Communication Flow

1. **Node 1 → Node 3**: Node 1 sends messages to Node 3
   - Packet travels: Node 1 → Node 2 → Node 3

2. **Node 3 → Node 1**: Node 3 sends replies back to Node 1
   - Packet travels: Node 3 → Node 2 → Node 1

3. **Node 2 (Router)**: Forwards all packets between Node 1 and Node 3
   - Receives packets from Node 1 destined for Node 3
   - Forwards them to Node 3
   - Receives replies from Node 3 destined for Node 1
   - Forwards them back to Node 1

## Key Features

### Multi-Instance Architecture

Each node runs its own independent protocol instance:
- Separate memory pools
- Separate route tables
- Separate statistics
- Complete isolation between instances

### Routing Configuration

**Node 1 Route Table**:
- Route to Node 2: Direct connection
- Route to Node 3: Via Node 2 (same PHY interface)

**Node 2 Route Table** (Router):
- Route to Node 1: Via PHY interface 1
- Route to Node 3: Via PHY interface 2

**Node 3 Route Table**:
- Route to Node 2: Direct connection
- Route to Node 1: Via Node 2 (same PHY interface)

### Packet Forwarding

Node 2 automatically forwards packets:
- Receives packet from Node 1 addressed to Node 3
- Looks up route to Node 3 in route table
- Forwards packet via appropriate PHY interface
- Same process for reverse direction

## Building

From the project root directory:

```bash
# Configure the build
cmake -B build -S .

# Build the multi-node example
cmake --build build --target multi_node

# Run the example
./build/examples/multi_node/multi_node
```

On Windows:

```cmd
cmake -B build -S .
cmake --build build --target multi_node --config Release
.\build\examples\multi_node\Release\multi_node.exe
```

## Code Structure

### Main Components

1. **Simulated Physical Layer**
   - Four communication channels (bidirectional between each pair of nodes)
   - Channel 1→2: Node 1 to Node 2
   - Channel 2→1: Node 2 to Node 1
   - Channel 2→3: Node 2 to Node 3
   - Channel 3→2: Node 3 to Node 2

2. **Node 1 (Source)**
   - Sends messages to Node 3
   - Receives replies from Node 3
   - One PHY interface (to Node 2)

3. **Node 2 (Router)**
   - Forwards packets between Node 1 and Node 3
   - Two PHY interfaces (one to Node 1, one to Node 3)
   - Route table maps destinations to appropriate interfaces

4. **Node 3 (Destination)**
   - Receives messages from Node 1
   - Sends replies back to Node 1
   - One PHY interface (to Node 2)

### Key API Functions Used

```c
/* Configuration with multiple routes */
xgl_route_item_t routes[] = {
    {
        .target_id = 2,
        .phy = &phy_interface_1,
        .max_frame_size = 256,
        .read_freq_hz = 100,
        .metric = 0
    },
    {
        .target_id = 3,
        .phy = &phy_interface_2,
        .max_frame_size = 256,
        .read_freq_hz = 100,
        .metric = 0
    }
};

config.route_table = routes;
config.route_table_len = 2;

/* Create multiple instances */
xgl_handle_t node1 = xgl_create(&node1_config);
xgl_handle_t node2 = xgl_create(&node2_config);
xgl_handle_t node3 = xgl_create(&node3_config);

/* Send to remote node (multi-hop) */
xgl_tx_data_t tx_data = {
    .target_id = 3,  /* Destination: Node 3 */
    .data_type = 0x01,
    .data = message,
    .data_len = len,
    .reliable = true,
    .priority = 0
};
xgl_send(node1, &tx_data);

/* Process all nodes */
xgl_run(node1, 100);
xgl_run(node2, 100);
xgl_run(node3, 100);
```

## Expected Output

```
=================================================
  xgen-link Multi-Node Network Example
=================================================
This example demonstrates a three-node network
with routing and packet forwarding.

Network Topology:
  Node 1 <-----> Node 2 <-----> Node 3
  [Source]       [Router]       [Destination]

Node 1 sends messages to Node 3 through Node 2.
Node 2 forwards packets between Node 1 and Node 3.
Node 3 receives messages and sends replies back.
=================================================

[INIT] Initializing communication channels...
[INIT] Channels initialized
[INIT] Configuring Node 1...
[INIT] Configuring Node 2 (Router)...
[INIT] Configuring Node 3...
[INIT] Creating Node 1 instance...
[INIT] Creating Node 2 instance...
[INIT] Creating Node 3 instance...
[INIT] Initializing Node 1...
[INIT] Initializing Node 2...
[INIT] Initializing Node 3...
[INIT] All nodes initialized successfully

=================================================
  Network Statistics
=================================================

[NODE 1 STATS] Protocol Statistics:
  TX Packets:    0
  TX Bytes:      0
  ...

=================================================
  Demonstrating Multi-Hop Communication
=================================================

[NODE 1] Sending message to Node 3: "Hello from Node 1!"
[NODE 1] Message sent successfully

[NODE 2] Received packet from Node 1 (20 bytes) - forwarding

[NODE 3] Received message from Node 1 (type 0x01, 20 bytes)
[NODE 3] Data: "Hello from Node 1!"
[NODE 3] Sent reply back to Node 1

[NODE 2] Received packet from Node 3 (17 bytes) - forwarding

[NODE 1] Received reply from Node 3 (type 0x02, 17 bytes)
[NODE 1] Data: "Reply from Node 3"

...

=================================================
  Network Statistics
=================================================

[NODE 1 STATS] Protocol Statistics:
  TX Packets:    3
  TX Bytes:      60
  RX Packets:    3
  RX Bytes:      51
  ...

[NODE 2 STATS] Protocol Statistics:
  TX Packets:    6
  TX Bytes:      111
  RX Packets:    6
  RX Bytes:      111
  ...

[NODE 3 STATS] Protocol Statistics:
  TX Packets:    3
  TX Bytes:      51
  RX Packets:    3
  RX Bytes:      60
  ...

=================================================
  Routing Analysis
=================================================
Expected behavior:
  - Node 1 sends packets to Node 3
  - Node 2 receives and forwards packets
  - Node 3 receives packets and sends replies
  - Node 2 forwards replies back to Node 1
  - Node 1 receives replies from Node 3

Check the statistics above to verify:
  - Node 1: TX packets = messages sent, RX packets = replies received
  - Node 2: TX + RX packets = forwarded traffic (should be highest)
  - Node 3: RX packets = messages received, TX packets = replies sent
=================================================

[CLEANUP] Destroying protocol instances...
[CLEANUP] Done

=================================================
  Multi-Node Network Example Complete
=================================================
```

## Understanding the Statistics

### Node 1 (Source)
- **TX Packets**: Number of messages sent to Node 3
- **RX Packets**: Number of replies received from Node 3
- **TX Bytes**: Total bytes sent
- **RX Bytes**: Total bytes received

### Node 2 (Router)
- **TX Packets**: Number of packets forwarded (should equal RX packets)
- **RX Packets**: Number of packets received for forwarding
- **TX + RX**: Should be approximately 2× the traffic of Node 1 or Node 3
- This node has the highest traffic as it forwards all packets

### Node 3 (Destination)
- **TX Packets**: Number of replies sent back to Node 1
- **RX Packets**: Number of messages received from Node 1
- **TX Bytes**: Total reply bytes sent
- **RX Bytes**: Total message bytes received

## Extending the Example

### 1. Add More Nodes

Create a larger network:

```
Node 1 <-> Node 2 <-> Node 3 <-> Node 4 <-> Node 5
```

Each intermediate node needs:
- Two PHY interfaces
- Route table entries for all reachable nodes

### 2. Create Mesh Topology

```
    Node 1 <-----> Node 2
      ^              ^
      |              |
      v              v
    Node 3 <-----> Node 4
```

Each node needs:
- Multiple PHY interfaces
- Route table with multiple paths
- Optional: Dynamic routing to select best path

### 3. Implement Dynamic Routing

```c
/* Add route at runtime */
xgl_route_add(handle, target_id, &phy, max_frame_size, metric);

/* Remove route */
xgl_route_remove(handle, target_id);

/* Update route metric (link quality) */
xgl_route_update_metric(handle, target_id, new_metric);
```

### 4. Add Route Discovery

Implement a simple route discovery protocol:
- Broadcast route request packets
- Intermediate nodes forward requests
- Destination sends route reply
- Source learns path and adds route

### 5. Implement Load Balancing

For nodes with multiple paths:
- Track link quality metrics
- Select best path based on RTT, error rate
- Distribute traffic across multiple paths

## Adapting for Real Hardware

### 1. Multi-Interface Router

```c
/* UART interface to Node 1 */
UART_Handle uart1 = UART_Init(UART1, 115200);
xgl_phy_ops_t phy_to_node1 = {
    .tx = uart1_tx,
    .rx = uart1_rx,
    .user_data = &uart1
};

/* UART interface to Node 3 */
UART_Handle uart2 = UART_Init(UART2, 115200);
xgl_phy_ops_t phy_to_node3 = {
    .tx = uart2_tx,
    .rx = uart2_rx,
    .user_data = &uart2
};

/* Configure router with both interfaces */
xgl_route_item_t routes[] = {
    { .target_id = 1, .phy = &phy_to_node1, ... },
    { .target_id = 3, .phy = &phy_to_node3, ... }
};
```

### 2. Mixed Physical Layers

```c
/* Node 2 with UART and CAN interfaces */
xgl_phy_ops_t uart_phy = { .tx = uart_tx, .rx = uart_rx, ... };
xgl_phy_ops_t can_phy = { .tx = can_tx, .rx = can_rx, ... };

xgl_route_item_t routes[] = {
    { .target_id = 1, .phy = &uart_phy, ... },  /* Node 1 via UART */
    { .target_id = 3, .phy = &can_phy, ... }    /* Node 3 via CAN */
};
```

### 3. Wireless Mesh Network

```c
/* Each node has a radio interface */
Radio_Handle radio = Radio_Init(2400);  /* 2.4 GHz */

xgl_phy_ops_t radio_phy = {
    .tx = radio_tx,
    .rx = radio_rx,
    .user_data = &radio
};

/* Configure routes to all neighbors */
xgl_route_item_t routes[] = {
    { .target_id = 2, .phy = &radio_phy, ... },
    { .target_id = 3, .phy = &radio_phy, ... },
    { .target_id = 4, .phy = &radio_phy, ... }
};
```

### 4. Main Loop for Real Hardware

```c
int main(void)
{
    /* Initialize hardware */
    UART_Init(...);
    Radio_Init(...);

    /* Create and initialize protocol instances */
    xgl_handle_t node = xgl_create(&config);
    xgl_init(node);

    /* Main loop */
    while (1) {
        /* Process protocol at 100 Hz */
        xgl_run(node, 100);

        /* Delay 10ms */
        delay_ms(10);

        /* Optional: Handle user input, update display, etc. */
    }

    return 0;
}
```

## Use Cases

### 1. Sensor Network

```
Gateway <-> Router 1 <-> Sensor 1
              |
              v
         Router 2 <-> Sensor 2
              |
              v
         Router 3 <-> Sensor 3
```

- Sensors send data to gateway through routers
- Gateway sends commands to sensors
- Routers forward packets and extend range

### 2. Industrial Automation

```
PLC <-> I/O Module 1 <-> I/O Module 2 <-> I/O Module 3
```

- PLC controls distributed I/O modules
- Modules forward commands along the chain
- Status updates flow back to PLC

### 3. Building Automation

```
Controller <-> Floor 1 Hub <-> Room 1 Devices
                  |
                  v
             Floor 2 Hub <-> Room 2 Devices
                  |
                  v
             Floor 3 Hub <-> Room 3 Devices
```

- Central controller manages building systems
- Floor hubs route commands to rooms
- Devices report status through hubs

### 4. Vehicle Network

```
ECU <-> Gateway <-> Body Control Module
                        |
                        v
                   Sensor Cluster
```

- ECU communicates with body control module
- Gateway routes between different bus types
- Sensors report to body control module

## Performance Considerations

### Latency

Multi-hop networks increase latency:
- Each hop adds processing delay
- More hops = higher end-to-end latency
- Minimize hops for time-critical data

### Throughput

Router nodes can become bottlenecks:
- Router processes packets for all nodes
- Increase router processing frequency
- Use larger buffers on routers
- Consider multiple paths for load balancing

### Memory Usage

Each node requires memory:
- TX/RX buffers
- Route table
- Packet queues
- Statistics

Router nodes need more memory:
- Handle traffic from multiple nodes
- Larger TX/RX pools
- More route table entries

## Troubleshooting

### Packets Not Reaching Destination

- Check route tables on all nodes
- Verify PHY interfaces are working
- Check that intermediate nodes are running
- Enable error callbacks to see routing errors

### Router Not Forwarding

- Verify router has routes to both source and destination
- Check that router's `xgl_run()` is being called
- Verify PHY interfaces are correctly configured
- Check router statistics for errors

### High Packet Loss

- Check CRC errors in statistics
- Verify physical layer reliability
- Increase retry count
- Check for buffer overflows on router

### Memory Issues

- Reduce buffer sizes on non-router nodes
- Increase router buffer sizes
- Use smaller route tables
- Monitor memory statistics

## Next Steps

After understanding this example, explore:

1. **Dynamic Routing** - Implement route discovery and updates
2. **Mesh Networks** - Create fully connected mesh topologies
3. **Quality of Service** - Prioritize critical traffic
4. **Network Monitoring** - Implement health checks and diagnostics

## References

- [xgen-link User Guide](../../docs/README.md)
- [API Documentation](../../include/xgl/xgl.h)
- [Echo Server Example](../echo_server/README.md)
- [File Transfer Example](../file_transfer/README.md)
- [Routing Guide](../../docs/README.md)

## License

Copyright (c) 2026 X-Gen Lab
