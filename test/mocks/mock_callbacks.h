/**
 * \file            mock_callbacks.h
 * \brief           Callback mock interface
 * \author          X-Gen Lab
 */

#ifndef MOCK_CALLBACKS_H
#define MOCK_CALLBACKS_H

#include <gmock/gmock.h>
#include <xgl/xgl.h>
#include <vector>
#include <string>

/**
 * \brief           Received data record
 */
struct RxRecord {
    uint16_t source_id;
    uint8_t data_type;
    std::vector<uint8_t> data;
};

/**
 * \brief           Error record
 */
struct ErrorRecord {
    xgl_error_t error;
    std::string message;
};

/**
 * \brief           Mock callback class for testing
 */
class MockCallbacks {
public:
    MockCallbacks() {}

    /**
     * \brief           Mock receive callback
     * \param[in]       handle: Instance handle
     * \param[in]       source_id: Source ID
     * \param[in]       data_type: Data type
     * \param[in]       data: Received data
     * \param[in]       len: Data length
     */
    MOCK_METHOD(void, rx_callback_impl,
                (xgl_handle_t handle, uint16_t source_id,
                 uint8_t data_type, const uint8_t* data, size_t len));

    /**
     * \brief           Mock error callback
     * \param[in]       handle: Instance handle
     * \param[in]       error: Error code
     * \param[in]       message: Error message
     */
    MOCK_METHOD(void, error_callback_impl,
                (xgl_handle_t handle, xgl_error_t error, const char* message));

    /**
     * \brief           Get C-style receive callback
     * \return          Receive callback function pointer
     */
    xgl_rx_callback_t get_rx_callback();

    /**
     * \brief           Get C-style error callback
     * \return          Error callback function pointer
     */
    xgl_error_callback_t get_error_callback();

    /**
     * \brief           Get received data records
     */
    const std::vector<RxRecord>& get_rx_records() const { return rx_records_; }

    /**
     * \brief           Get error records
     */
    const std::vector<ErrorRecord>& get_error_records() const { return error_records_; }

    /**
     * \brief           Clear all records
     */
    void clear_records() {
        rx_records_.clear();
        error_records_.clear();
    }

private:
    static void rx_callback_wrapper(xgl_handle_t handle, uint16_t source_id,
                                    uint8_t data_type,
                                    const uint8_t* data, size_t len,
                                    void* user_data);

    static void error_callback_wrapper(xgl_handle_t handle, xgl_error_t error,
                                       const char* message, void* user_data);

    std::vector<RxRecord> rx_records_;
    std::vector<ErrorRecord> error_records_;

    static thread_local MockCallbacks* current_instance_;
};

#endif /* MOCK_CALLBACKS_H */
