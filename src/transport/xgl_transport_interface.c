/**
 * \file            xgl_transport_interface.c
 * \brief           Typed packet receive boundary
 */

#include "xgl_transport_internal.h"

/**
 * \brief           Deliver a borrowed network packet to transport
 * \param[in,out]   ctx: Transport context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       packet: Borrowed received packet
 * \return          Transport receive result
 */
static xgl_error_t transport_receive_impl(void* ctx, xgl_handle_t handle,
                                          const xgl_packet_t* packet) {
    return xgl_transport_receive((xgl_transport_ctx_t*)ctx, handle, packet);
}

/**
 * \brief           Initialize the typed transport receive boundary
 * \param[in,out]   ctx: Transport context
 * \param[out]      iface: Packet interface initialized with receive only
 * \return          XGL_OK or invalid argument error
 */
xgl_error_t xgl_transport_get_interface(xgl_transport_ctx_t* ctx,
                                        xgl_packet_interface_t* iface) {
    if (ctx == NULL || iface == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    xgl_packet_interface_init(iface, ctx, NULL, transport_receive_impl);
    return XGL_OK;
}
