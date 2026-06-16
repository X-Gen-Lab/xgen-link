/**
 * \file            mock_phy.h
 * \brief           Physical layer mock interface
 * \author          X-Gen Lab
 */

#ifndef MOCK_PHY_H
#define MOCK_PHY_H

#include <gmock/gmock.h>
#include <xgl/xgl.h>
#include <vector>
#include <cstring>

/**
 * \brief           Mock physical layer class for testing
 */
class MockPhy {
public:
    MockPhy() : tx_count_(0), rx_count_(0) {}

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
    const std::vector<uint8_t>& get_tx_data() const { return tx_data_; }

    /**
     * \brief           Clear transmitted data
     */
    void clear_tx_data() { tx_data_.clear(); }

    /**
     * \brief           Get transmission count
     */
    size_t get_tx_count() const { return tx_count_; }

    /**
     * \brief           Get reception count
     */
    size_t get_rx_count() const { return rx_count_; }

private:
    static xgl_error_t tx_wrapper(const uint8_t* data, size_t len, void* user_data);
    static xgl_error_t rx_wrapper(uint8_t* buffer, size_t* len, void* user_data);

    xgl_phy_ops_t phy_ops_;
    std::vector<uint8_t> tx_data_;
    std::vector<uint8_t> rx_queue_;
    size_t tx_count_;
    size_t rx_count_;

    static thread_local MockPhy* current_instance_;
};

#endif /* MOCK_PHY_H */
