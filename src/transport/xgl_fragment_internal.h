/**
 * \file            xgl_fragment_internal.h
 * \brief           Private fragment reassembly helpers
 */

#ifndef XGL_FRAGMENT_INTERNAL_H
#define XGL_FRAGMENT_INTERNAL_H

#include <transport/xgl_fragment.h>
#include <xgl/xgl_config.h>

/**
 * \brief           Free a buffer through its owning manager
 * \param[in,out]   manager: Live owning manager when buffer is non-NULL
 * \param[in,out]   buffer: Detached buffer to release, or NULL
 */
void fragment_free_reassembly_buffer(xgl_fragment_manager_t* manager,
                                     xgl_reassembly_buffer_t* buffer);

xgl_error_t fragment_insert_received_range(xgl_reassembly_buffer_t* buffer,
                                           size_t start, size_t end);

#if XGL_FEATURE_ADVANCED_RUNTIME
/**
 * \brief           Release one expired message before admitting another byte
 * \param[in,out]   manager: Reassembly owner
 * \param[in]       source_id: Remote endpoint
 * \param[in]       connection_id: Connection identity
 * \param[in]       session_epoch: Session identity
 * \param[in]       message_id: Exact message identity
 * \param[in]       now_ms: Current explicit transport time
 * \return          true only when a matching expired record was released
 */
bool fragment_expire_message(xgl_fragment_manager_t* manager,
                             uint16_t source_id, uint32_t connection_id,
                             uint32_t session_epoch, uint32_t message_id,
                             uint32_t now_ms);
#endif

#endif /* XGL_FRAGMENT_INTERNAL_H */
