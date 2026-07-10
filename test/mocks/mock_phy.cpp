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

/*---------------------------------------------------------------------------*/
/* LoopbackPhyPair Implementation                                            */
/*---------------------------------------------------------------------------*/

xgl_phy_ops_t LoopbackPhyPair::get_phy_a() {
    xgl_phy_ops_t ops = {};
    ops.tx = tx_a;
    ops.rx = rx_a;
    ops.user_data = this;
    return ops;
}

xgl_phy_ops_t LoopbackPhyPair::get_phy_b() {
    xgl_phy_ops_t ops = {};
    ops.tx = tx_b;
    ops.rx = rx_b;
    ops.user_data = this;
    return ops;
}

xgl_error_t LoopbackPhyPair::tx_a(const uint8_t* data, size_t len, void* user_data) {
    auto* self = static_cast<LoopbackPhyPair*>(user_data);
    if (self == nullptr || data == nullptr) {
        return XGL_ERR_NULL_POINTER;
    }
    std::lock_guard<std::mutex> lock(self->ab_mutex_);
    self->ab_queue_.emplace_back(data, data + len);
    self->a_tx_count_++;
    return XGL_OK;
}

xgl_error_t LoopbackPhyPair::rx_a(uint8_t* buffer, size_t* len, void* user_data) {
    auto* self = static_cast<LoopbackPhyPair*>(user_data);
    if (self == nullptr || buffer == nullptr || len == nullptr) {
        return XGL_ERR_NULL_POINTER;
    }
    std::lock_guard<std::mutex> lock(self->ba_mutex_);
    if (self->ba_queue_.empty()) {
        *len = 0;
        return XGL_OK;
    }
    const auto& frame = self->ba_queue_.front();
    size_t copy_len = std::min(*len, frame.size());
    std::memcpy(buffer, frame.data(), copy_len);
    self->ba_queue_.pop_front();
    *len = copy_len;
    return XGL_OK;
}

xgl_error_t LoopbackPhyPair::tx_b(const uint8_t* data, size_t len, void* user_data) {
    auto* self = static_cast<LoopbackPhyPair*>(user_data);
    if (self == nullptr || data == nullptr) {
        return XGL_ERR_NULL_POINTER;
    }
    std::lock_guard<std::mutex> lock(self->ba_mutex_);
    self->ba_queue_.emplace_back(data, data + len);
    self->b_tx_count_++;
    return XGL_OK;
}

xgl_error_t LoopbackPhyPair::rx_b(uint8_t* buffer, size_t* len, void* user_data) {
    auto* self = static_cast<LoopbackPhyPair*>(user_data);
    if (self == nullptr || buffer == nullptr || len == nullptr) {
        return XGL_ERR_NULL_POINTER;
    }
    std::lock_guard<std::mutex> lock(self->ab_mutex_);
    if (self->ab_queue_.empty()) {
        *len = 0;
        return XGL_OK;
    }
    const auto& frame = self->ab_queue_.front();
    size_t copy_len = std::min(*len, frame.size());
    std::memcpy(buffer, frame.data(), copy_len);
    self->ab_queue_.pop_front();
    *len = copy_len;
    return XGL_OK;
}

/*---------------------------------------------------------------------------*/
/* FifoPhy Implementation                                                    */
/*---------------------------------------------------------------------------*/

xgl_phy_ops_t FifoPhy::get_phy_ops() {
    xgl_phy_ops_t ops = {};
    ops.tx = tx_cb;
    ops.rx = rx_cb;
    ops.user_data = this;
    return ops;
}

void FifoPhy::enqueue_rx(const uint8_t* data, size_t len) {
    std::lock_guard<std::mutex> lock(mutex_);
    rx_queue_.emplace_back(data, data + len);
}

xgl_error_t FifoPhy::tx_cb(const uint8_t* data, size_t len, void* user_data) {
    auto* self = static_cast<FifoPhy*>(user_data);
    if (self == nullptr || data == nullptr) {
        return XGL_ERR_NULL_POINTER;
    }
    std::lock_guard<std::mutex> lock(self->mutex_);
    self->tx_log_.emplace_back(data, data + len);
    self->tx_count_++;
    return XGL_OK;
}

xgl_error_t FifoPhy::rx_cb(uint8_t* buffer, size_t* len, void* user_data) {
    auto* self = static_cast<FifoPhy*>(user_data);
    if (self == nullptr || buffer == nullptr || len == nullptr) {
        return XGL_ERR_NULL_POINTER;
    }
    std::lock_guard<std::mutex> lock(self->mutex_);
    if (self->rx_queue_.empty()) {
        *len = 0;
        return XGL_OK;
    }
    const auto& frame = self->rx_queue_.front();
    size_t copy_len = std::min(*len, frame.size());
    std::memcpy(buffer, frame.data(), copy_len);
    self->rx_queue_.pop_front();
    *len = copy_len;
    return XGL_OK;
}
