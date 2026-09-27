/* Single definition of the failure counter declared extern in test_util.h.
 * Every test executable links this once, alongside its own test_*.c. */
#include "test_util.h"

int g_failures = 0;
