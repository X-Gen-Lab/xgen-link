# Migrate to SDK 4

Build SDK 4 from the repository root. Clear old CMake caches and install prefixes, then [rebuild each consumer](build-and-consume.md) with the generated header and library for the same profile.

## Build and public ABI

Continue linking `xgl::xgl`. Use the CMake export and `xgl_build_config.h` for capabilities and build identity. Remove the old source assembly and foundation source arguments from product scripts. Products can still select foundation packages for their own libraries.

Dynamic creation uses `xgl_allocator_t` from the XGL public headers. Declare `ctx`, `alloc` and `free` using the new descriptor type. Successful allocations satisfy `xgl_max_align_t` alignment. To adapt an existing provider, create a new descriptor and keep it and its context alive until destruction. Use type-compatible callbacks and pointers.

Static initialization still measures storage through `xgl_memory_requirements()`. Recheck RAM, alignment and status under the new toolchain and configuration. The SDK 4 checked entry points validate configuration size, ABI and build identity.

## Send and receive behavior changes

- Install an acceptance callback where the instance needs to receive DATA. Products that previously discarded DATA with empty callbacks can install an explicit accept-and-discard callback. Send-only endpoints can still process ACK and CONTROL with empty callbacks.
- Check `xgl_send_at()` for synchronous fragmented-send failures and `xgl_step*()` for maintenance failures. If the scope fails, fragments already accepted by the PHY may still be in transit. Follow the [send and recovery contract](send.md) for closing the old scope and retrying application work.
- Advanced send operations use the same scope deadline. A layout query prepares storage and the actual send checks the deadline again.
- Check the [MTU in both directions](../reference/protocol/v3/routing.md) for reliable traffic. A missing local reverse route or insufficient feedback capacity is reported before acceptance.
- For a custom compiled frame ceiling, set `-DXGL_DATALINK_MAX_FRAME_SIZE=<bytes>` when configuring the library. Install the resulting header and library together, then rebuild consumers.

## Wire compatibility

The wire version remains v3. Both endpoints must support the frames, extensions and authentication policy in use. See [Interoperability](../reference/protocol/v3/interoperability.md) and [Validation](../development/validation.md).

The historical SDK 3 assembly is documented in the [previous migration guide](https://github.com/X-Gen-Lab/xgen-link/blob/f09efca25abe84c344ce8c41d5645006c7b61df9/docs/en/guide/modular-migration.md).
