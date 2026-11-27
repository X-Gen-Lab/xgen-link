# Zero-Copy

The zero-copy send APIs build a single unreliable frame in caller-owned writable storage. They do not introduce asynchronous buffer ownership.

## Supported Scope

| API | Scope and purpose |
| --- | --- |
| `xgl_send_zerocopy_at()` | Legacy descriptor; connection ID and session epoch are both zero |
| `xgl_send_zerocopy_scoped_at()` | `xgl_tx_zerocopy_request_t` selects an explicit connection ID and session epoch |
| `xgl_send_zerocopy_layout()` | Queries the exact payload offset and complete frame size for the scoped request |

Both send APIs support only single-frame, unreliable transmission. `reliable=true` is rejected with `XGL_ERR_INVALID_PARAM`, and `timeout_ms` is ignored. Use `xgl_send_at()` for reliable DATA. Authenticated sending requires a trusted session installed for the selected target, connection ID and session epoch.

## Buffer Layout

Reserve exact headroom before the payload and trailer capacity after it:

| Part | Bytes |
| --- | ---: |
| Base header | `XGL_FRAME_HEADER_SIZE` (24) |
| Nonzero data type | `XGL_DATA_TYPE_EXT_SIZE` (3) |
| Nonzero session epoch | `XGL_SESSION_EXT_SIZE` (14) |
| Authentication security extension, if enabled | 15 |
| Authentication tag, if enabled | Provider tag length |
| Final CRC | `XGL_CRC16_SIZE` (2) |

The connection ID is carried in the base header and adds no extension bytes. Obtain the layout from `xgl_send_zerocopy_layout()` instead of duplicating these calculations in application code. `data_offset` must equal `layout.payload_offset`, and `buffer_size` must cover `layout.frame_size`. The complete frame must fit both the route MTU and the instance frame limit. The stack writes the header and trailer; the payload remains at its original address.

## Plan and Send

The following C11 example assumes an initialized instance with a route to node 2. If authentication is required, install the corresponding trusted session before querying the layout.

```c
#include <string.h>
#include <xgl/xgl.h>

xgl_error_t send_scoped_example(xgl_handle_t handle, uint32_t now_ms) {
    const uint8_t payload[] = {1U, 2U, 3U, 4U};
    uint8_t storage[128];
    xgl_tx_zerocopy_request_t request = {0};
    request.data.target_id = 2U;
    request.data.data_type = 1U;
    request.data.data_len = sizeof(payload);
    request.connection_id = 11U;
    request.session_epoch = 9U;

    xgl_tx_frame_layout_t layout;
    xgl_error_t error = xgl_send_zerocopy_layout(handle, &request, &layout);
    if (error != XGL_OK) {
        return error;
    }
    if (layout.frame_size > sizeof(storage)) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    memcpy(storage + layout.payload_offset, payload, sizeof(payload));
    request.data.buffer = storage;
    request.data.buffer_size = sizeof(storage);
    request.data.data_offset = layout.payload_offset;
    return xgl_send_zerocopy_scoped_at(handle, &request, now_ms);
}
```

The query ignores `buffer`, `buffer_size` and `data_offset`; `data_len` must be nonzero. It clears the output layout on failure. It does not allocate, call the security provider or PHY, update transmission statistics, or consume a security sequence. A successful query reserves no resources. The eventual send revalidates the current route, limits and security state.

## Ownership

The caller may reuse storage after either send API returns. All callbacks borrow it synchronously. PHY TX must finish reading or copying it before returning, even when backed by DMA. This interface reduces frame assembly copies; a driver's private DMA copy may still be necessary. Do not retain borrowed pointers or pass immutable storage.

## Failure Handling

Check the returned status. A local success is not evidence of remote delivery. A failure can leave the header and trailer modified. Once an authenticated send reserves a security sequence, later provider or PHY failure does not roll it back; retry through the API instead of replaying the previously serialized bytes. See [Security](../reference/protocol/v1/security.md).

Calls, queries and retries must follow the instance's serialization rules. A layout query or rejection during basic descriptor validation is not a transmission attempt. Errors after entering the common network send path are counted according to the [statistics contract](../reference/statistics.md); the returned error code alone does not identify which validation stage failed.
