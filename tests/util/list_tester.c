#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <unity/unity.h>

#include "util.h"

typedef struct
{
    int a;
    int b;
    listnode_t node;
} test_container;

void setUp(void)
{
}

void tearDown(void)
{
}

void test_list_empty(void)
{
    listnode_t head = LIST_HEAD_INIT(head);

    TEST_ASSERT_TRUE(list_empty(&head));
}

void test_add_del(void)
{
    listnode_t head = LIST_HEAD_INIT(head);
    test_container cont;

    cont.a = 1;
    cont.b = 2;

    TEST_ASSERT_TRUE(list_empty(&head));

    list_add_tail(&head, &cont.node);

    TEST_ASSERT_FALSE(list_empty(&head));

    list_remove(&cont.node);

    TEST_ASSERT_TRUE(list_empty(&head));
}

void test_list_add_tail_iterate(void)
{
    listnode_t head = LIST_HEAD_INIT(head);
    test_container v1, v2;
    int i;

    listnode_t *ptr;

    v1.a = 1;
    v1.b = 1;

    v2.a = 2;
    v2.b = 2;

    list_add_tail(&head, &v1.node);
    list_add_tail(&head, &v2.node);

    i = 1;
    list_iterate(&head, ptr)
    {
        test_container *cont = list_container(ptr, test_container, node);

        TEST_ASSERT_EQUAL_INT(i, cont->a);
        TEST_ASSERT_EQUAL_INT(i, cont->b);

        ++i;
    }
}

void test_list_add_head_iterate(void)
{
    listnode_t head = LIST_HEAD_INIT(head);
    test_container v1, v2;
    int i;

    listnode_t *ptr;

    v1.a = 1;
    v1.b = 1;

    v2.a = 2;
    v2.b = 2;

    list_add_head(&head, &v1.node);
    list_add_head(&head, &v2.node);

    i = 2;
    list_iterate(&head, ptr)
    {
        test_container *cont = list_container(ptr, test_container, node);

        TEST_ASSERT_EQUAL_INT(i, cont->a);
        TEST_ASSERT_EQUAL_INT(i, cont->b);

        --i;
    }
}

void test_list_add_2_remove_1_tail(void)
{
    listnode_t head = LIST_HEAD_INIT(head);
    test_container v1, v2;
    int i;

    listnode_t *ptr;

    v1.a = 1;
    v1.b = 1;

    v2.a = 2;
    v2.b = 2;

    list_add_head(&head, &v1.node);
    list_add_head(&head, &v2.node);
    list_remove(&v1.node);

}

int main(int argc, char *argv[])
{
    UNITY_BEGIN();
    RUN_TEST(test_list_empty);
    RUN_TEST(test_add_del);
    RUN_TEST(test_list_add_tail_iterate);
    RUN_TEST(test_list_add_head_iterate);
    return UNITY_END();
}
