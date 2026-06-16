/**
 * \file            mock_phy.cpp
 * \brief           Physical layer mock implementation
 * \author          X-Gen Lab
 */

#include "mock_phy.h"

/* Thread-local storage for current mock instance */
thread_local MockPhy* MockPhy::current_instance_ = nullptr;

xgl_phy_ops_t* MockPhy::get_phy_ops() {
    phy_ops_.tx = tx_wrapper;
    phy_ops_.rx = rx_wrapper;
    phy_ops_.user_data = this;
    current_instance_ = this;
    return &phy_ops_;
}

void MockPhy::queue_rx_data(const uint8_t* data, size_t len) {
    rx_queue_.insert(rx_queue_.end(), data, data + len);
}

xgl_error_t MockPhy::tx_wrapper(const uint8_t* data, size_t len, void* user_data) {
    MockPhy* instance = static_cast<MockPhy*>(user_data);
    if (instance == nullptr) {
        return XGL_ERR_NOT_INITIALIZED;
    }

    /* Store transmitted data */
    instance->tx_data_.insert(
        instance->tx_data_.end(),
        data,
        data + len
    );
    instance->tx_count_++;

    /* Call mock implementation */
    return instance->tx_impl(data, len);
}

xgl_error_t MockPhy::rx_wrapper(uint8_t* buffer, size_t* len, void* user_data) {
    MockPhy* instance = static_cast<MockPhy*>(user_data);
    if (instance == nullptr) {
        return XGL_ERR_NOT_INITIALIZED;
    }

    instance->rx_count_++;

    /* Return queued data if available */
    if (!instance->rx_queue_.empty()) {
        size_t copy_len = std::min(*len, instance->rx_queue_.size());
        std::memcpy(buffer, instance->rx_queue_.data(), copy_len);
        instance->rx_queue_.erase(
            instance->rx_queue_.begin(),
            instance->rx_queue_.begin() + copy_len
        );
        *len = copy_len;
        return XGL_OK;
    }

    /* Call mock implementation */
    return instance->rx_impl(buffer, len);
}
