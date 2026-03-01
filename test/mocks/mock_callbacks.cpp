/**
 * \file            mock_callbacks.cpp
 * \brief           Callback mock implementation
 * \author          Nexus Team
 */

#include "mock_callbacks.h"

/* Thread-local storage for current mock instance */
thread_local MockCallbacks* MockCallbacks::current_instance_ = nullptr;

xgl_rx_callback_t MockCallbacks::get_rx_callback() {
    current_instance_ = this;
    return rx_callback_wrapper;
}

xgl_error_callback_t MockCallbacks::get_error_callback() {
    current_instance_ = this;
    return error_callback_wrapper;
}

void MockCallbacks::rx_callback_wrapper(xgl_handle_t handle, uint8_t source_id,
                                       uint8_t data_type,
                                       const uint8_t* data, size_t len,
                                       void* user_data) {
    (void)user_data;  /* Unused parameter */
    
    if (current_instance_ == nullptr) {
        return;
    }
    
    /* Record the received data */
    RxRecord record;
    record.source_id = source_id;
    record.data_type = data_type;
    record.data.assign(data, data + len);
    current_instance_->rx_records_.push_back(record);
    
    /* Call mock implementation */
    current_instance_->rx_callback_impl(handle, source_id,
                                       data_type, data, len);
}

void MockCallbacks::error_callback_wrapper(xgl_handle_t handle, xgl_error_t error,
                                          const char* message, void* user_data) {
    (void)user_data;  /* Unused parameter */
    
    if (current_instance_ == nullptr) {
        return;
    }
    
    /* Record the error */
    ErrorRecord record;
    record.error = error;
    record.message = message ? message : "";
    current_instance_->error_records_.push_back(record);
    
    /* Call mock implementation */
    current_instance_->error_callback_impl(handle, error, message);
}
