# Thread Safety

One instance executes in one serialized context. The protocol has no internal locks, thread-safe container variants, global mutex service, or runtime thread-safety switch.

## Serialization Boundary

Serialize every call using the same handle, including send, step, timeout queries, session changes, stats, and destroy. Prefer an owner task/event loop. If several tasks access a handle, the application must provide one consistent locking policy.

Different instances may run independently when their workspaces and application contexts are independent. A shared PHY, allocator, or security provider still needs the application's own synchronization.

## Callback Rules

PHY, receive, authentication, and error callbacks execute synchronously inside an API call. They must not reenter, reset, close, or destroy that instance. Queue requested work and process it after the outer call returns. This rule also applies when using a recursive application lock.

Receive buffers are borrowed only during the callback. Copy accepted data to application-owned storage before returning if it must survive. PHY TX must finish consuming or copying frame bytes before return.

## Interrupt Boundary

ISRs should capture bytes into application storage and signal an owner task. Do not call the parser, send, or `xgl_step()` concurrently with task execution. The application is responsible for ring-buffer atomicity and ISR/task visibility.

## Shutdown

Stop new submissions, exclude concurrent API calls, and finish the current callback before calling `xgl_destroy()`. Only then release configuration, provider/PHY descriptors, contexts, and caller workspace.
