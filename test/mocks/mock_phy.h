/**
 * \file            mock_phy.h
 * \brief           Physical layer mock interface
 * \author          X-Gen Lab
 */

#ifndef MOCK_PHY_H
#define MOCK_PHY_H

#include <cstring>
#include <deque>
#include <gmock/gmock.h>
#include <mutex>
#include <vector>
#include <xgl/xgl.h>

/**
 * \brief           Mock physical layer class for testing
 */
class MockPhy {
  public:
    MockPhy() : tx_count_(0), rx_count_(0) {
    }

    /**
     * \brief           Mock transmit function
     * \param[in]       data: Data to transmit
     * \param[in]       len: Data length
     * \return          XGL_OK on success
     */
    MOCK_METHOD(xgl_error_t, tx_impl, (const uint8_t* data, size_t len));

    /**
     * \brief           Mock receive function
     * \param[out]      buffer: Buffer to receive data
     * \param[in,out]   len: Buffer size / received length
     * \return          XGL_OK on success
     */
    MOCK_METHOD(xgl_error_t, rx_impl, (uint8_t* buffer, size_t* len));

    /**
     * \brief           Get C-style PHY operations interface
     * \return          PHY operations structure
     */
    xgl_phy_ops_t* get_phy_ops();

    /**
     * \brief           Add data to receive queue
     * \param[in]       data: Data to queue
     * \param[in]       len: Data length
     */
    void queue_rx_data(const uint8_t* data, size_t len);

    /**
     * \brief           Get transmitted data
     * \return          Vector of transmitted bytes
     */
    const std::vector<uint8_t>& get_tx_data() const {
        return tx_data_;
    }

    /**
     * \brief           Clear transmitted data
     */
    void clear_tx_data() {
        tx_data_.clear();
    }

    /**
     * \brief           Get transmission count
     */
    size_t get_tx_count() const {
        return tx_count_;
    }

    /**
     * \brief           Get reception count
     */
    size_t get_rx_count() const {
        return rx_count_;
    }

  private:
    static xgl_error_t tx_wrapper(const uint8_t* data, size_t len,
                                  void* user_data);
    static xgl_error_t rx_wrapper(uint8_t* buffer, size_t* len,
                                  void* user_data);

    xgl_phy_ops_t phy_ops_;
    std::vector<uint8_t> tx_data_;
    std::vector<uint8_t> rx_queue_;
    size_t tx_count_;
    size_t rx_count_;

    static thread_local MockPhy* current_instance_;
};

/**
 * \brief           Loopback PHY pair that wires two instances together
 * \details         Instance A's TX connects to instance B's RX buffer and vice
 * versa. TX callback enqueues frame bytes; RX callback dequeues them.
 */
class LoopbackPhyPair {
  public:
    LoopbackPhyPair() = default;

    /**
     * \brief           Get PHY ops for node A
     * \details         A's tx goes to B's rx queue; A's rx reads from A's
     * incoming queue
     */
    xgl_phy_ops_t get_phy_a();

    /**
     * \brief           Get PHY ops for node B
     * \details         B's tx goes to A's rx queue; B's rx reads from B's
     * incoming queue
     */
    xgl_phy_ops_t get_phy_b();

    /**
     * \brief           Get total frames sent by A
     */
    size_t get_a_tx_count() const {
        return a_tx_count_;
    }

    /**
     * \brief           Get total frames sent by B
     */
    size_t get_b_tx_count() const {
        return b_tx_count_;
    }

  private:
    static xgl_error_t tx_a(const uint8_t* data, size_t len, void* user_data);
    static xgl_error_t rx_a(uint8_t* buffer, size_t* len, void* user_data);
    static xgl_error_t tx_b(const uint8_t* data, size_t len, void* user_data);
    static xgl_error_t rx_b(uint8_t* buffer, size_t* len, void* user_data);

    /* Queue from A->B (A's tx feeds B's rx) */
    std::mutex ab_mutex_;
    std::deque<std::vector<uint8_t>> ab_queue_;

    /* Queue from B->A (B's tx feeds A's rx) */
    std::mutex ba_mutex_;
    std::deque<std::vector<uint8_t>> ba_queue_;

    size_t a_tx_count_ = 0;
    size_t b_tx_count_ = 0;
};

/**
 * \brief           Simple FIFO PHY for single-direction loopback
 * \details         tx enqueues frames, rx dequeues them. Used for multi-hop
 * chains.
 */
class FifoPhy {
  public:
    FifoPhy() = default;

    xgl_phy_ops_t get_phy_ops();

    /** Enqueue a frame into this PHY's RX queue */
    void enqueue_rx(const uint8_t* data, size_t len);

    /** Get number of frames transmitted */
    size_t get_tx_count() const {
        return tx_count_;
    }

  private:
    static xgl_error_t tx_cb(const uint8_t* data, size_t len, void* user_data);
    static xgl_error_t rx_cb(uint8_t* buffer, size_t* len, void* user_data);

    std::mutex mutex_;
    std::deque<std::vector<uint8_t>> rx_queue_;
    std::vector<std::vector<uint8_t>> tx_log_;
    size_t tx_count_ = 0;
};

#endif /* MOCK_PHY_H */
