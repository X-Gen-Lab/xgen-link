/**
 * \file            benchmark_support.h
 * \brief           Persistent two-node benchmark fixture with explicit protocol
 * time
 * \author          X-Gen Lab
 */
#ifndef XGL_BENCHMARK_SUPPORT_H
#define XGL_BENCHMARK_SUPPORT_H
#include <xgl/xgl.h>

#include <mock_phy.h>
#include <xgen/memory/libc_allocator.h>

/** \brief           Own all configuration and driver state for the entire
 * benchmark. */
class BenchmarkPair {
  public:
    LoopbackPhyPair link;
    xgl_phy_ops_t phy_a = link.get_phy_a();
    xgl_phy_ops_t phy_b = link.get_phy_b();
    xgl_route_item_t route_a = {};
    xgl_route_item_t route_b = {};
    xgl_config_t config_a = {};
    xgl_config_t config_b = {};
    xgl_handle_t a = nullptr;
    xgl_handle_t b = nullptr;
    uint32_t now_ms = 0U;
    size_t delivered = 0U;

    /** \brief           Release instances before their borrowed configuration
     * is destroyed. */
    ~BenchmarkPair() {
        if (a != nullptr) {
            xgl_destroy(a);
        }
        if (b != nullptr) {
            xgl_destroy(b);
        }
    }

    /**
     * \brief           Initialize both nodes with the same persistent resource
     * configuration
     * \param[in]       preset: Valid unauthenticated configuration
     * \return          True when both nodes are initialized
     */
    bool init(const xgl_config_t& preset) {
        config_a = preset;
        config_b = preset;
        config_a.source_id = 1U;
        config_b.source_id = 2U;
        route_a = {2U, &phy_a, preset.protocol.max_frame_size, 1000U, 1U};
        route_b = {1U, &phy_b, preset.protocol.max_frame_size, 1000U, 1U};
        config_a.route_table = &route_a;
        config_b.route_table = &route_b;
        config_a.route_table_len = config_b.route_table_len = 1U;
        config_a.memory.allocator = config_b.memory.allocator =
            xgm_allocator_libc();
        config_b.rx_callback = [](xgl_handle_t, uint16_t, uint8_t,
                                  const uint8_t*, size_t, void* context) {
            ++static_cast<BenchmarkPair*>(context)->delivered;
        };
        config_b.callback_user_data = this;
        a = xgl_create(&config_a);
        b = xgl_create(&config_b);
        return a != nullptr && b != nullptr && xgl_init(a) == XGL_OK &&
               xgl_init(b) == XGL_OK;
    }

    /**
     * \brief           Time-independent send and drain through actual
     * application delivery
     * \param[in]       payload: Borrowed payload valid until return
     * \param[in]       length: Payload bytes
     * \param[in]       reliable: Request transport ACK and retain a
     * retransmission copy
     * \return          True only after delivery and a final sender
     * ACK-processing step
     */
    bool exchange(const uint8_t* payload, size_t length, bool reliable) {
        xgl_tx_data_t tx = {};
        tx.target_id = 2U;
        tx.data = payload;
        tx.data_len = length;
        tx.reliable = reliable;
        if (xgl_send_at(a, &tx, now_ms) != XGL_OK) {
            return false;
        }
        const xgl_work_budget_t budget = {2048U, 1000U};
        const size_t expected = delivered + 1U;
        for (unsigned cycle = 0U; cycle < 1024U && delivered < expected;
             ++cycle) {
            if (xgl_step(a, now_ms, &budget) != XGL_OK ||
                xgl_step(b, now_ms, &budget) != XGL_OK) {
                return false;
            }
            ++now_ms;
        }
        return delivered == expected &&
               xgl_step(a, now_ms++, &budget) == XGL_OK;
    }
};
#endif
