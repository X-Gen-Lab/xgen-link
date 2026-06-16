/**
 * \file            mock_allocator.cpp
 * \brief           Allocator mock implementation
 * \author          X-Gen Lab
 */

#include "mock_allocator.h"

/* Thread-local storage for current mock instance */
thread_local MockAllocator* MockAllocator::current_instance_ = nullptr;
