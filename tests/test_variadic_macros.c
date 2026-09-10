#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "public/errors.h"
#include "private/base.h"

static int allocations;
static int actions;
static int callbacks;
static int count_evaluations;
static bool fail_allocation;

static void *checked_calloc(size_t count, size_t size)
{
    ++allocations;
    return fail_allocation ? NULL : calloc(count, size);
}

static void log_error(kburn_err_t error, const char *action)
{
    assert(error != KBurnNoErr);
    assert(action != NULL);
    ++callbacks;
}

#undef default_log
#define default_log(error, action) log_error(error, action)
#define calloc(count, size) checked_calloc(count, size)

static size_t array_count(void)
{
    ++count_evaluations;
    return 3;
}

static kburn_err_t allocate_one(void)
{
    int *value = MyAlloc(int);
    assert(*value == 0);
    free(value);
    return KBurnNoErr;
}

static kburn_err_t allocate_array(void)
{
    int *values = MyAlloc(int, array_count());
    assert(values[0] == 0 && values[1] == 0 && values[2] == 0);
    free(values);
    return KBurnNoErr;
}

static kburn_err_t action_result(kburn_err_t result)
{
    ++actions;
    return result;
}

static bool nonnegative(kburn_err_t result)
{
    return result >= 0;
}

static kburn_err_t check_one(kburn_err_t result)
{
    IfErrorReturn(action_result(result));
    return 100;
}

static kburn_err_t check_two(kburn_err_t result)
{
    IfErrorReturn(nonnegative, action_result(result));
    return 100;
}

static kburn_err_t check_three(kburn_err_t result)
{
    IfErrorReturn(nonnegative, action_result(result), log_error);
    return 100;
}

int main(void)
{
    assert(allocate_one() == KBurnNoErr);
    assert(allocate_array() == KBurnNoErr);
    fail_allocation = true;
    assert(allocate_one() == KBurnNoMemory);
    assert(allocate_array() == KBurnNoMemory);
    assert(allocations == 4 && count_evaluations == 2);

    assert(check_one(KBurnNoErr) == 100);
    assert(check_one(KBurnWiredError) == KBurnWiredError);
    assert(check_two(1) == 100);
    assert(check_two(-1) == -1);
    assert(check_three(1) == 100);
    assert(check_three(-1) == -1);
    assert(actions == 6 && callbacks == 3);
    return 0;
}
