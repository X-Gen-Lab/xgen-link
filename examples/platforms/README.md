# Platform Port Examples

These files are reference ports, not default host build targets.

- `bare_metal_port.c`: polling main loop with a ring buffer style UART boundary.
- `freertos_port.c`: task-based integration pattern.
- `windows_mock_port.c`: host mock PHY for SDK and CI checks.

Copy the closest example into the board support package and replace the stub hardware calls with target drivers.
