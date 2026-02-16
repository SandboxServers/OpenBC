#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_count = 0;
static int test_pass  = 0;
static int test_fail  = 0;

#define TEST(name) \
    static void test_##name(void); \
    static void run_test_##name(void) { \
        test_count++; \
        printf("  [%d] %s... ", test_count, #name); \
        test_##name(); \
        test_pass++; \
        printf("ok\n"); \
    } \
    static void test_##name(void)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        test_fail++; \
        test_pass--; /* undo the pre-increment in run_ wrapper */ \
        return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("FAIL\n    %s:%d: %s == 0x%X, expected 0x%X\n", \
               __FILE__, __LINE__, #a, (unsigned)(a), (unsigned)(b)); \
        test_fail++; \
        test_pass--; \
        return; \
    } \
} while(0)

#define ASSERT_EQ_INT(a, b) do { \
    if ((a) != (b)) { \
        printf("FAIL\n    %s:%d: %s == %d, expected %d\n", \
               __FILE__, __LINE__, #a, (int)(a), (int)(b)); \
        test_fail++; \
        test_pass--; \
        return; \
    } \
} while(0)

#define RUN(name) run_test_##name()

#define TEST_MAIN_BEGIN() int main(void) { \
    printf("Running %s\n", __FILE__);

#define TEST_MAIN_END() \
    printf("%d/%d tests passed\n", test_pass, test_count); \
    return test_fail > 0 ? 1 : 0; \
}

#endif /* TEST_UTIL_H */
