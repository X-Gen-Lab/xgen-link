/**
 * \file            mock_allocator.h
 * \brief           Allocator mock interface
 * \author          X-Gen Lab
 */

#ifndef MOCK_ALLOCATOR_H
#define MOCK_ALLOCATOR_H

#include <xgl/xgl.h>

#include <cstdlib>
#include <gmock/gmock.h>
#include <map>

/**
 * \brief           Mock allocator class for testing custom allocator usage
 */
class MockAllocator {
  public:
    MockAllocator() : total_allocated_(0), total_freed_(0) {
    }

    /**
     * \brief           Mock malloc function
     */
    MOCK_METHOD(void*, malloc_impl, (size_t size));

    /**
     * \brief           Mock free function
     */
    MOCK_METHOD(void, free_impl, (void* ptr));

    /**
     * \brief           Get C-style allocator interface
     */
    const xgm_allocator_t* get_allocator() {
        service_.ctx = this;
        service_.alloc = [](void* ctx, size_t size) -> void* {
            return static_cast<MockAllocator*>(ctx)->malloc_wrapper(size);
        };
        service_.free = [](void* ctx, void* ptr) {
            static_cast<MockAllocator*>(ctx)->free_wrapper(ptr);
        };
        return &service_;
    }

    /**
     * \brief           Get allocation statistics
     */
    size_t get_total_allocated() const {
        return total_allocated_;
    }

    size_t get_total_freed() const {
        return total_freed_;
    }

    size_t get_current_allocated() const {
        return total_allocated_ - total_freed_;
    }

    size_t get_alloc_count() const {
        return allocations_.size();
    }

  private:
    void* malloc_wrapper(size_t size) {
        void* ptr = malloc_impl(size);
        if (ptr != nullptr) {
            allocations_[ptr] = size;
            total_allocated_ += size;
        }
        return ptr;
    }

    void free_wrapper(void* ptr) {
        if (ptr != nullptr) {
            auto it = allocations_.find(ptr);
            if (it != allocations_.end()) {
                total_freed_ += it->second;
                allocations_.erase(it);
            }
            free_impl(ptr);
        }
    }

    xgm_allocator_t service_{};
    std::map<void*, size_t> allocations_;
    size_t total_allocated_;
    size_t total_freed_;
};

#endif /* MOCK_ALLOCATOR_H */
