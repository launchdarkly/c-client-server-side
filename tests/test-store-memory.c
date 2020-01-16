#include <launchdarkly/api.h>

#include "misc.h"

#include "util-store.h"

static struct LDStore *
prepareEmptyStore()
{
    struct LDStore *store;
    struct LDConfig *config;

    LD_ASSERT(config = LDConfigNew(""));
    LD_ASSERT(store = LDStoreNew(config));
    LD_ASSERT(!LDStoreInitialized(store));

    LDConfigFree(config);

    return store;
}

int
main()
{
    LDConfigureGlobalLogger(LD_LOG_TRACE, LDBasicLogger);
    LDGlobalInit();

    runSharedStoreTests(prepareEmptyStore);

    return 0;
}
