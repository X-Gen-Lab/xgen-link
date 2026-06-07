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
