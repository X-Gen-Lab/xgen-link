/**
 * \file            mock_allocator.h
 * \brief           Allocator mock interface
 * \author          Nexus Team
 */

#ifndef MOCK_ALLOCATOR_H
#define MOCK_ALLOCATOR_H

#include <gmock/gmock.h>
#include <xgl/xgl.h>
#include <map>
#include <cstdlib>

/**
 * \brief           Mock allocator class for testing custom allocator usage
 */
class MockAllocator {
public:
    MockAllocator() : total_allocated_(0), total_freed_(0) {}
    
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
    xgl_allocator_t* get_allocator() {
        allocator_.malloc = [](size_t size) -> void* {
            return current_instance_->malloc_wrapper(size);
        };
        allocator_.free = [](void* ptr) {
            current_instance_->free_wrapper(ptr);
        };
        allocator_.user_data = this;
        current_instance_ = this;
        return &allocator_;
    }
    
    /**
     * \brief           Get allocation statistics
     */
    size_t get_total_allocated() const { return total_allocated_; }
    size_t get_total_freed() const { return total_freed_; }
    size_t get_current_allocated() const { return total_allocated_ - total_freed_; }
    size_t get_alloc_count() const { return allocations_.size(); }
    
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
    
    xgl_allocator_t allocator_;
    std::map<void*, size_t> allocations_;
    size_t total_allocated_;
    size_t total_freed_;
    
    static thread_local MockAllocator* current_instance_;
};

#endif /* MOCK_ALLOCATOR_H */
