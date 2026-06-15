/**
 * \file            test_mutex.cpp
 * \brief           Mutex abstraction unit tests
 * \author          Nexus Team
 */

#include <gtest/gtest.h>
#include <xgl/internal/xgl_mutex.h>

/*---------------------------------------------------------------------------*/
/* Basic Mutex Tests                                                         */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test mutex initialization
 */
TEST(XglMutexTest, InitSuccess) {
    xgl_mutex_t mutex;
    
    xgl_error_t result = xgl_mutex_init(&mutex);
    EXPECT_EQ(result, XGL_OK);
    
    xgl_mutex_destroy(&mutex);
}

/**
 * \brief           Test mutex initialization with NULL pointer
 */
TEST(XglMutexTest, InitNullPointer) {
    xgl_error_t result = xgl_mutex_init(NULL);
    EXPECT_EQ(result, XGL_ERR_NULL_POINTER);
}

/**
 * \brief           Test mutex lock and unlock
 */
TEST(XglMutexTest, LockUnlock) {
    xgl_mutex_t mutex;
    
    ASSERT_EQ(xgl_mutex_init(&mutex), XGL_OK);
    
    /* Lock mutex */
    EXPECT_EQ(xgl_mutex_lock(&mutex), XGL_OK);
    
    /* Unlock mutex */
    EXPECT_EQ(xgl_mutex_unlock(&mutex), XGL_OK);
    
    xgl_mutex_destroy(&mutex);
}

/**
 * \brief           Test mutex lock with NULL pointer
 */
TEST(XglMutexTest, LockNullPointer) {
    xgl_error_t result = xgl_mutex_lock(NULL);
    EXPECT_EQ(result, XGL_ERR_NULL_POINTER);
}

/**
 * \brief           Test mutex unlock with NULL pointer
 */
TEST(XglMutexTest, UnlockNullPointer) {
    xgl_error_t result = xgl_mutex_unlock(NULL);
    EXPECT_EQ(result, XGL_ERR_NULL_POINTER);
}

/**
 * \brief           Test mutex trylock
 */
TEST(XglMutexTest, TryLock) {
    xgl_mutex_t mutex;
    
    ASSERT_EQ(xgl_mutex_init(&mutex), XGL_OK);
    
    /* First trylock should succeed */
    EXPECT_EQ(xgl_mutex_trylock(&mutex), XGL_OK);
    
    /* Unlock */
    EXPECT_EQ(xgl_mutex_unlock(&mutex), XGL_OK);
    
    xgl_mutex_destroy(&mutex);
}

/**
 * \brief           Test mutex trylock with NULL pointer
 */
TEST(XglMutexTest, TryLockNullPointer) {
    xgl_error_t result = xgl_mutex_trylock(NULL);
    EXPECT_EQ(result, XGL_ERR_NULL_POINTER);
}

/**
 * \brief           Test mutex destroy
 */
TEST(XglMutexTest, Destroy) {
    xgl_mutex_t mutex;
    
    ASSERT_EQ(xgl_mutex_init(&mutex), XGL_OK);
    
    /* Destroy should not crash */
    xgl_mutex_destroy(&mutex);
    
    /* Destroying NULL should not crash */
    xgl_mutex_destroy(NULL);
}

/*---------------------------------------------------------------------------*/
/* Mutex Guard Tests                                                         */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test mutex guard lock and unlock
 */
TEST(XglMutexTest, GuardLockUnlock) {
    xgl_mutex_t mutex;
    
    ASSERT_EQ(xgl_mutex_init(&mutex), XGL_OK);
    
    /* Create guard (locks mutex) */
    xgl_mutex_guard_t guard = xgl_mutex_guard_lock(&mutex);
    EXPECT_TRUE(guard.locked);
    EXPECT_EQ(guard.mutex, &mutex);
    
    /* Release guard (unlocks mutex) */
    xgl_mutex_guard_unlock(&guard);
    EXPECT_FALSE(guard.locked);
    
    xgl_mutex_destroy(&mutex);
}

/**
 * \brief           Test mutex guard with NULL pointer
 */
TEST(XglMutexTest, GuardNullPointer) {
    xgl_mutex_guard_t guard = xgl_mutex_guard_lock(NULL);
    EXPECT_FALSE(guard.locked);
    EXPECT_EQ(guard.mutex, nullptr);
    
    /* Unlock should not crash */
    xgl_mutex_guard_unlock(&guard);
}

/**
 * \brief           Test mutex guard unlock with NULL
 */
TEST(XglMutexTest, GuardUnlockNull) {
    /* Should not crash */
    xgl_mutex_guard_unlock(NULL);
}

/*---------------------------------------------------------------------------*/
/* Recursive Lock Tests (Platform-Specific)                                  */
/*---------------------------------------------------------------------------*/

#ifdef XGL_THREAD_SAFE

/**
 * \brief           Test recursive locking (same thread locks multiple times)
 */
TEST(XglMutexTest, RecursiveLock) {
    xgl_mutex_t mutex;
    
    ASSERT_EQ(xgl_mutex_init(&mutex), XGL_OK);
    
    /* Lock multiple times (recursive) */
    EXPECT_EQ(xgl_mutex_lock(&mutex), XGL_OK);
    EXPECT_EQ(xgl_mutex_lock(&mutex), XGL_OK);
    EXPECT_EQ(xgl_mutex_lock(&mutex), XGL_OK);
    
    /* Unlock same number of times */
    EXPECT_EQ(xgl_mutex_unlock(&mutex), XGL_OK);
    EXPECT_EQ(xgl_mutex_unlock(&mutex), XGL_OK);
    EXPECT_EQ(xgl_mutex_unlock(&mutex), XGL_OK);
    
    xgl_mutex_destroy(&mutex);
}

#endif /* XGL_THREAD_SAFE */

/*---------------------------------------------------------------------------*/
/* Error Condition Tests                                                     */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test locking uninitialized mutex
 */
TEST(XglMutexTest, LockUninitialized) {
    xgl_mutex_t mutex = {0};
    
#ifdef XGL_THREAD_SAFE
    /* On thread-safe platforms, should return error */
    xgl_error_t result = xgl_mutex_lock(&mutex);
    EXPECT_EQ(result, XGL_ERR_NOT_INITIALIZED);
#else
    /* On bare-metal, should succeed (no-op) */
    xgl_error_t result = xgl_mutex_lock(&mutex);
    EXPECT_EQ(result, XGL_OK);
#endif
}

/**
 * \brief           Test unlocking uninitialized mutex
 */
TEST(XglMutexTest, UnlockUninitialized) {
    xgl_mutex_t mutex = {0};
    
#ifdef XGL_THREAD_SAFE
    /* On thread-safe platforms, should return error */
    xgl_error_t result = xgl_mutex_unlock(&mutex);
    EXPECT_EQ(result, XGL_ERR_NOT_INITIALIZED);
#else
    /* On bare-metal, should succeed (no-op) */
    xgl_error_t result = xgl_mutex_unlock(&mutex);
    EXPECT_EQ(result, XGL_OK);
#endif
}

/*---------------------------------------------------------------------------*/
/* Integration Tests                                                         */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test multiple mutex instances
 */
TEST(XglMutexTest, MultipleInstances) {
    xgl_mutex_t mutex1, mutex2, mutex3;
    
    /* Initialize all mutexes */
    ASSERT_EQ(xgl_mutex_init(&mutex1), XGL_OK);
    ASSERT_EQ(xgl_mutex_init(&mutex2), XGL_OK);
    ASSERT_EQ(xgl_mutex_init(&mutex3), XGL_OK);
    
    /* Lock all mutexes */
    EXPECT_EQ(xgl_mutex_lock(&mutex1), XGL_OK);
    EXPECT_EQ(xgl_mutex_lock(&mutex2), XGL_OK);
    EXPECT_EQ(xgl_mutex_lock(&mutex3), XGL_OK);
    
    /* Unlock in different order */
    EXPECT_EQ(xgl_mutex_unlock(&mutex2), XGL_OK);
    EXPECT_EQ(xgl_mutex_unlock(&mutex1), XGL_OK);
    EXPECT_EQ(xgl_mutex_unlock(&mutex3), XGL_OK);
    
    /* Destroy all mutexes */
    xgl_mutex_destroy(&mutex1);
    xgl_mutex_destroy(&mutex2);
    xgl_mutex_destroy(&mutex3);
}

/**
 * \brief           Test mutex with guard pattern
 */
TEST(XglMutexTest, GuardPattern) {
    xgl_mutex_t mutex;
    
    ASSERT_EQ(xgl_mutex_init(&mutex), XGL_OK);
    
    /* Use guard in nested scope */
    {
        xgl_mutex_guard_t guard = xgl_mutex_guard_lock(&mutex);
        EXPECT_TRUE(guard.locked);
        
        /* Do some work while locked */
        int value = 42;
        EXPECT_EQ(value, 42);
        
        /* Guard will unlock when released */
        xgl_mutex_guard_unlock(&guard);
        EXPECT_FALSE(guard.locked);
    }
    
    /* Mutex should be unlocked now, can lock again */
    EXPECT_EQ(xgl_mutex_lock(&mutex), XGL_OK);
    EXPECT_EQ(xgl_mutex_unlock(&mutex), XGL_OK);
    
    xgl_mutex_destroy(&mutex);
}
