/**
 * \file            test_list.cpp
 * \brief           List data structure unit tests
 * \author          Nexus Team
 */

#include <gtest/gtest.h>
#include <xgl/internal/xgl_list.h>

/*---------------------------------------------------------------------------*/
/* Test Data Structure                                                       */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test data structure with embedded list node
 */
typedef struct {
    int value;
    xgl_list_node_t node;
} test_item_t;

/*---------------------------------------------------------------------------*/
/* List Initialization Tests                                                 */
/*---------------------------------------------------------------------------*/

TEST(XglListTest, InitializeList) {
    xgl_list_t list;
    xgl_list_init(&list);
    
    EXPECT_TRUE(xgl_list_is_empty(&list));
    EXPECT_EQ(xgl_list_count(&list), 0);
    EXPECT_EQ(xgl_list_peek_head(&list), nullptr);
    EXPECT_EQ(xgl_list_peek_tail(&list), nullptr);
}

TEST(XglListTest, InitializeNode) {
    xgl_list_node_t node;
    xgl_list_node_init(&node);
    
    EXPECT_EQ(node.next, nullptr);
    EXPECT_EQ(node.prev, nullptr);
}

TEST(XglListTest, InitializeWithNull) {
    /* Should not crash */
    xgl_list_init(nullptr);
    xgl_list_node_init(nullptr);
    
    SUCCEED();
}

/*---------------------------------------------------------------------------*/
/* List Insertion Tests                                                      */
/*---------------------------------------------------------------------------*/

TEST(XglListTest, InsertHead) {
    xgl_list_t list;
    xgl_list_init(&list);
    
    test_item_t item1 = {1, {}};
    test_item_t item2 = {2, {}};
    test_item_t item3 = {3, {}};
    
    xgl_list_insert_head(&list, &item1.node);
    EXPECT_EQ(xgl_list_count(&list), 1);
    EXPECT_EQ(xgl_list_peek_head(&list), &item1.node);
    EXPECT_EQ(xgl_list_peek_tail(&list), &item1.node);
    
    xgl_list_insert_head(&list, &item2.node);
    EXPECT_EQ(xgl_list_count(&list), 2);
    EXPECT_EQ(xgl_list_peek_head(&list), &item2.node);
    EXPECT_EQ(xgl_list_peek_tail(&list), &item1.node);
    
    xgl_list_insert_head(&list, &item3.node);
    EXPECT_EQ(xgl_list_count(&list), 3);
    EXPECT_EQ(xgl_list_peek_head(&list), &item3.node);
    EXPECT_EQ(xgl_list_peek_tail(&list), &item1.node);
}

TEST(XglListTest, InsertTail) {
    xgl_list_t list;
    xgl_list_init(&list);
    
    test_item_t item1 = {1, {}};
    test_item_t item2 = {2, {}};
    test_item_t item3 = {3, {}};
    
    xgl_list_insert_tail(&list, &item1.node);
    EXPECT_EQ(xgl_list_count(&list), 1);
    EXPECT_EQ(xgl_list_peek_head(&list), &item1.node);
    EXPECT_EQ(xgl_list_peek_tail(&list), &item1.node);
    
    xgl_list_insert_tail(&list, &item2.node);
    EXPECT_EQ(xgl_list_count(&list), 2);
    EXPECT_EQ(xgl_list_peek_head(&list), &item1.node);
    EXPECT_EQ(xgl_list_peek_tail(&list), &item2.node);
    
    xgl_list_insert_tail(&list, &item3.node);
    EXPECT_EQ(xgl_list_count(&list), 3);
    EXPECT_EQ(xgl_list_peek_head(&list), &item1.node);
    EXPECT_EQ(xgl_list_peek_tail(&list), &item3.node);
}

TEST(XglListTest, InsertAfter) {
    xgl_list_t list;
    xgl_list_init(&list);
    
    test_item_t item1 = {1, {}};
    test_item_t item2 = {2, {}};
    test_item_t item3 = {3, {}};
    
    xgl_list_insert_head(&list, &item1.node);
    xgl_list_insert_after(&list, &item1.node, &item2.node);
    
    EXPECT_EQ(xgl_list_count(&list), 2);
    EXPECT_EQ(xgl_list_peek_head(&list), &item1.node);
    EXPECT_EQ(xgl_list_peek_tail(&list), &item2.node);
    EXPECT_EQ(item1.node.next, &item2.node);
    EXPECT_EQ(item2.node.prev, &item1.node);
    
    xgl_list_insert_after(&list, &item1.node, &item3.node);
    
    EXPECT_EQ(xgl_list_count(&list), 3);
    EXPECT_EQ(item1.node.next, &item3.node);
    EXPECT_EQ(item3.node.prev, &item1.node);
    EXPECT_EQ(item3.node.next, &item2.node);
    EXPECT_EQ(item2.node.prev, &item3.node);
}

TEST(XglListTest, InsertBefore) {
    xgl_list_t list;
    xgl_list_init(&list);
    
    test_item_t item1 = {1, {}};
    test_item_t item2 = {2, {}};
    test_item_t item3 = {3, {}};
    
    xgl_list_insert_head(&list, &item1.node);
    xgl_list_insert_before(&list, &item1.node, &item2.node);
    
    EXPECT_EQ(xgl_list_count(&list), 2);
    EXPECT_EQ(xgl_list_peek_head(&list), &item2.node);
    EXPECT_EQ(xgl_list_peek_tail(&list), &item1.node);
    EXPECT_EQ(item2.node.next, &item1.node);
    EXPECT_EQ(item1.node.prev, &item2.node);
    
    xgl_list_insert_before(&list, &item1.node, &item3.node);
    
    EXPECT_EQ(xgl_list_count(&list), 3);
    EXPECT_EQ(item3.node.next, &item1.node);
    EXPECT_EQ(item1.node.prev, &item3.node);
    EXPECT_EQ(item3.node.prev, &item2.node);
    EXPECT_EQ(item2.node.next, &item3.node);
}

TEST(XglListTest, InsertWithNull) {
    xgl_list_t list;
    xgl_list_init(&list);
    
    test_item_t item = {1, {}};
    
    /* Should not crash */
    xgl_list_insert_head(nullptr, &item.node);
    xgl_list_insert_head(&list, nullptr);
    xgl_list_insert_tail(nullptr, &item.node);
    xgl_list_insert_tail(&list, nullptr);
    
    EXPECT_EQ(xgl_list_count(&list), 0);
}

/*---------------------------------------------------------------------------*/
/* List Removal Tests                                                        */
/*---------------------------------------------------------------------------*/

TEST(XglListTest, RemoveNode) {
    xgl_list_t list;
    xgl_list_init(&list);
    
    test_item_t item1 = {1, {}};
    test_item_t item2 = {2, {}};
    test_item_t item3 = {3, {}};
    
    xgl_list_insert_tail(&list, &item1.node);
    xgl_list_insert_tail(&list, &item2.node);
    xgl_list_insert_tail(&list, &item3.node);
    
    /* Remove middle node */
    xgl_list_remove(&list, &item2.node);
    EXPECT_EQ(xgl_list_count(&list), 2);
    EXPECT_EQ(item1.node.next, &item3.node);
    EXPECT_EQ(item3.node.prev, &item1.node);
    EXPECT_EQ(item2.node.next, nullptr);
    EXPECT_EQ(item2.node.prev, nullptr);
    
    /* Remove head */
    xgl_list_remove(&list, &item1.node);
    EXPECT_EQ(xgl_list_count(&list), 1);
    EXPECT_EQ(xgl_list_peek_head(&list), &item3.node);
    EXPECT_EQ(xgl_list_peek_tail(&list), &item3.node);
    
    /* Remove last node */
    xgl_list_remove(&list, &item3.node);
    EXPECT_EQ(xgl_list_count(&list), 0);
    EXPECT_TRUE(xgl_list_is_empty(&list));
}

TEST(XglListTest, RemoveHead) {
    xgl_list_t list;
    xgl_list_init(&list);
    
    test_item_t item1 = {1, {}};
    test_item_t item2 = {2, {}};
    test_item_t item3 = {3, {}};
    
    xgl_list_insert_tail(&list, &item1.node);
    xgl_list_insert_tail(&list, &item2.node);
    xgl_list_insert_tail(&list, &item3.node);
    
    xgl_list_node_t* node = xgl_list_remove_head(&list);
    EXPECT_EQ(node, &item1.node);
    EXPECT_EQ(xgl_list_count(&list), 2);
    EXPECT_EQ(xgl_list_peek_head(&list), &item2.node);
    
    node = xgl_list_remove_head(&list);
    EXPECT_EQ(node, &item2.node);
    EXPECT_EQ(xgl_list_count(&list), 1);
    
    node = xgl_list_remove_head(&list);
    EXPECT_EQ(node, &item3.node);
    EXPECT_EQ(xgl_list_count(&list), 0);
    EXPECT_TRUE(xgl_list_is_empty(&list));
    
    /* Remove from empty list */
    node = xgl_list_remove_head(&list);
    EXPECT_EQ(node, nullptr);
}

TEST(XglListTest, RemoveTail) {
    xgl_list_t list;
    xgl_list_init(&list);
    
    test_item_t item1 = {1, {}};
    test_item_t item2 = {2, {}};
    test_item_t item3 = {3, {}};
    
    xgl_list_insert_tail(&list, &item1.node);
    xgl_list_insert_tail(&list, &item2.node);
    xgl_list_insert_tail(&list, &item3.node);
    
    xgl_list_node_t* node = xgl_list_remove_tail(&list);
    EXPECT_EQ(node, &item3.node);
    EXPECT_EQ(xgl_list_count(&list), 2);
    EXPECT_EQ(xgl_list_peek_tail(&list), &item2.node);
    
    node = xgl_list_remove_tail(&list);
    EXPECT_EQ(node, &item2.node);
    EXPECT_EQ(xgl_list_count(&list), 1);
    
    node = xgl_list_remove_tail(&list);
    EXPECT_EQ(node, &item1.node);
    EXPECT_EQ(xgl_list_count(&list), 0);
    EXPECT_TRUE(xgl_list_is_empty(&list));
    
    /* Remove from empty list */
    node = xgl_list_remove_tail(&list);
    EXPECT_EQ(node, nullptr);
}

TEST(XglListTest, RemoveWithNull) {
    xgl_list_t list;
    xgl_list_init(&list);
    
    test_item_t item = {1, {}};
    xgl_list_insert_head(&list, &item.node);
    
    /* Should not crash */
    xgl_list_remove(nullptr, &item.node);
    xgl_list_remove(&list, nullptr);
    
    EXPECT_EQ(xgl_list_count(&list), 1);
    
    EXPECT_EQ(xgl_list_remove_head(nullptr), nullptr);
    EXPECT_EQ(xgl_list_remove_tail(nullptr), nullptr);
}

/*---------------------------------------------------------------------------*/
/* List Navigation Tests                                                     */
/*---------------------------------------------------------------------------*/

TEST(XglListTest, NavigateList) {
    xgl_list_t list;
    xgl_list_init(&list);
    
    test_item_t item1 = {1, {}};
    test_item_t item2 = {2, {}};
    test_item_t item3 = {3, {}};
    
    xgl_list_insert_tail(&list, &item1.node);
    xgl_list_insert_tail(&list, &item2.node);
    xgl_list_insert_tail(&list, &item3.node);
    
    /* Forward navigation */
    xgl_list_node_t* node = xgl_list_peek_head(&list);
    EXPECT_EQ(node, &item1.node);
    
    node = xgl_list_next(node);
    EXPECT_EQ(node, &item2.node);
    
    node = xgl_list_next(node);
    EXPECT_EQ(node, &item3.node);
    
    node = xgl_list_next(node);
    EXPECT_EQ(node, nullptr);
    
    /* Backward navigation */
    node = xgl_list_peek_tail(&list);
    EXPECT_EQ(node, &item3.node);
    
    node = xgl_list_prev(node);
    EXPECT_EQ(node, &item2.node);
    
    node = xgl_list_prev(node);
    EXPECT_EQ(node, &item1.node);
    
    node = xgl_list_prev(node);
    EXPECT_EQ(node, nullptr);
}

TEST(XglListTest, NavigateWithNull) {
    EXPECT_EQ(xgl_list_peek_head(nullptr), nullptr);
    EXPECT_EQ(xgl_list_peek_tail(nullptr), nullptr);
    EXPECT_EQ(xgl_list_next(nullptr), nullptr);
    EXPECT_EQ(xgl_list_prev(nullptr), nullptr);
}

/*---------------------------------------------------------------------------*/
/* List Iteration Tests                                                      */
/*---------------------------------------------------------------------------*/

TEST(XglListTest, IterateForward) {
    xgl_list_t list;
    xgl_list_init(&list);
    
    test_item_t items[5];
    for (int i = 0; i < 5; i++) {
        items[i].value = i;
        xgl_list_insert_tail(&list, &items[i].node);
    }
    
    int count = 0;
    xgl_list_node_t* node;
    XGL_LIST_FOR_EACH(&list, node) {
        test_item_t* item = XGL_LIST_ENTRY(node, test_item_t, node);
        EXPECT_EQ(item->value, count);
        count++;
    }
    
    EXPECT_EQ(count, 5);
}

TEST(XglListTest, IterateSafe) {
    xgl_list_t list;
    xgl_list_init(&list);
    
    test_item_t items[5];
    for (int i = 0; i < 5; i++) {
        items[i].value = i;
        xgl_list_insert_tail(&list, &items[i].node);
    }
    
    /* Remove all nodes during iteration */
    xgl_list_node_t* node;
    xgl_list_node_t* tmp;
    int count = 0;
    XGL_LIST_FOR_EACH_SAFE(&list, node, tmp) {
        xgl_list_remove(&list, node);
        count++;
    }
    
    EXPECT_EQ(count, 5);
    EXPECT_TRUE(xgl_list_is_empty(&list));
}

/*---------------------------------------------------------------------------*/
/* Container Access Tests                                                    */
/*---------------------------------------------------------------------------*/

TEST(XglListTest, ContainerAccess) {
    test_item_t item = {42, {}};
    
    xgl_list_node_t* node = &item.node;
    test_item_t* retrieved = XGL_LIST_ENTRY(node, test_item_t, node);
    
    EXPECT_EQ(retrieved, &item);
    EXPECT_EQ(retrieved->value, 42);
}

/*---------------------------------------------------------------------------*/
/* Edge Case Tests                                                           */
/*---------------------------------------------------------------------------*/

TEST(XglListTest, SingleNodeOperations) {
    xgl_list_t list;
    xgl_list_init(&list);
    
    test_item_t item = {1, {}};
    
    xgl_list_insert_head(&list, &item.node);
    EXPECT_EQ(xgl_list_peek_head(&list), &item.node);
    EXPECT_EQ(xgl_list_peek_tail(&list), &item.node);
    EXPECT_EQ(item.node.next, nullptr);
    EXPECT_EQ(item.node.prev, nullptr);
    
    xgl_list_remove(&list, &item.node);
    EXPECT_TRUE(xgl_list_is_empty(&list));
}

TEST(XglListTest, EmptyListOperations) {
    xgl_list_t list;
    xgl_list_init(&list);
    
    EXPECT_TRUE(xgl_list_is_empty(&list));
    EXPECT_EQ(xgl_list_count(&list), 0);
    EXPECT_EQ(xgl_list_peek_head(&list), nullptr);
    EXPECT_EQ(xgl_list_peek_tail(&list), nullptr);
    EXPECT_EQ(xgl_list_remove_head(&list), nullptr);
    EXPECT_EQ(xgl_list_remove_tail(&list), nullptr);
}

TEST(XglListTest, LargeList) {
    xgl_list_t list;
    xgl_list_init(&list);
    
    const int N = 1000;
    test_item_t* items = new test_item_t[N];
    
    /* Insert N items */
    for (int i = 0; i < N; i++) {
        items[i].value = i;
        xgl_list_insert_tail(&list, &items[i].node);
    }
    
    EXPECT_EQ(xgl_list_count(&list), N);
    
    /* Verify order */
    int count = 0;
    xgl_list_node_t* node;
    XGL_LIST_FOR_EACH(&list, node) {
        test_item_t* item = XGL_LIST_ENTRY(node, test_item_t, node);
        EXPECT_EQ(item->value, count);
        count++;
    }
    
    EXPECT_EQ(count, N);
    
    /* Remove all items */
    for (int i = 0; i < N; i++) {
        xgl_list_remove(&list, &items[i].node);
    }
    
    EXPECT_TRUE(xgl_list_is_empty(&list));
    
    delete[] items;
}
