/**
 * \file            layout_probe.c
 * \brief           Export target ABI sizes without linking the diagnostic
 *                  object
 * \author          X-Gen Lab
 */

#include "footprint_config.h"

/* The workspace type is deliberately private to the implementation. Including
 * its source here measures that exact type without publishing a second ABI.
 * This object is inspected with objcopy and never linked into the image. */
#include "../../src/api/xgl_workspace.c"

_Static_assert(XGL_INIT_CLASSES == 3U && XGL_RESOURCE_COUNT == 5U,
               "Update the footprint layout for the new initialization plan");
_Static_assert(XGL_FEATURE_AUTH == 0 && XGL_FEATURE_FRAGMENTATION == 0 &&
                   XGL_FEATURE_FORWARDING == 0 && XGL_FEATURE_OUT_OF_ORDER == 0,
               "This layout describes only the bounded Boot profile");
_Static_assert(XGL_FEATURE_ROUTE_INDEX == 0,
               "Boot routes must not retain a hash index");
_Static_assert(XGL_ALLOW_FALLBACK_MALLOC == 0,
               "The footprint image must not use the libc heap backend");

__attribute__((used, section(".xgl_footprint_layout")))
const uint32_t xgl_boot_target_layout[] = {UINT32_C(0x58474c42),
                                           3U,
                                           sizeof(void*),
                                           _Alignof(xgm_max_align_t),
                                           sizeof(xgl_workspace_t),
                                           sizeof(xgl_route_item_t),
                                           sizeof(xgl_instance_link_t),
                                           sizeof(xgl_transport_peer_state_t),
                                           sizeof(xgl_reliable_packet_t),
                                           sizeof(struct xgl_instance),
                                           sizeof(xgl_datalink_ctx_t),
                                           sizeof(xgl_transport_ctx_t),
                                           XGL_FOOTPRINT_RX_SIZE,
                                           XGL_FOOTPRINT_FRAME_SIZE,
                                           XGL_FOOTPRINT_PEERS,
                                           XGL_FOOTPRINT_ROUTES,
                                           XGL_FOOTPRINT_LINKS,
                                           XGL_FOOTPRINT_WINDOW,
                                           XGL_FOOTPRINT_TX_PACKETS,
                                           XGL_FOOTPRINT_SCRATCH_BLOCKS,
                                           (XGL_FOOTPRINT_WINDOW + 7U) / 8U,
                                           XGL_WIRE_BASE_HEADER_SIZE,
                                           XGL_CRC16_SIZE};
