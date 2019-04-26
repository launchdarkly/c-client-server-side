#include <launchdarkly/api.h>

#include "config.h"
#include "client.h"
#include "evaluate.h"
#include "streaming.h"
#include "store.h"
#include "misc.h"

static void
testParsePathFlags()
{
    enum FeatureKind kind;
    const char *key;

    LD_ASSERT(LDi_parsePath("/flags/abcd", &kind, &key));

    LD_ASSERT(kind == LD_FLAG);
    LD_ASSERT(strcmp(key, "abcd") == 0);
}

static void
testParsePathSegments()
{
    enum FeatureKind kind;
    const char *key;

    LD_ASSERT(LDi_parsePath("/segments/xyz", &kind, &key));

    LD_ASSERT(kind == LD_SEGMENT);
    LD_ASSERT(strcmp(key, "xyz") == 0);
}

static void
testParsePathUnknownKind()
{
    enum FeatureKind kind;
    const char *key;

    LD_ASSERT(!LDi_parsePath("/unknown/123", &kind, &key));

    LD_ASSERT(!key);
}

static void
testInitialPut(struct StreamContext *const context)
{
    struct LDJSONRC *flag, *segment;

    const char *const event = "event: put";

    const char *const body =
        "data: {\"path\": \"/\", \"data\": {\"flags\": {\"my-flag\":"
        "{\"key\": \"my-flag\", \"version\": 2}},\"segments\": "
        "{\"my-segment\": {\"key\": \"my-segment\", \"version\": 5}}}}";

    LD_ASSERT(context);

    LD_ASSERT(LDi_onSSE(context, event));
    LD_ASSERT(LDi_onSSE(context, body));
    LD_ASSERT(LDi_onSSE(context, ""));

    LD_ASSERT(LDStoreGet(
        context->client->config->store, LD_FLAG, "my-flag", &flag));
    LD_ASSERT(flag);
    LD_ASSERT(LDGetNumber(LDObjectLookup(LDJSONRCGet(flag), "version")) == 2);

    LD_ASSERT(LDStoreGet(
        context->client->config->store, LD_SEGMENT, "my-segment", &segment));
    LD_ASSERT(segment);
    LD_ASSERT(LDGetNumber(LDObjectLookup(LDJSONRCGet(segment), "version")) ==
        5);

    LDJSONRCDecrement(flag);
    LDJSONRCDecrement(segment);
}

static void
testPatchFlag(struct StreamContext *const context)
{
    struct LDJSONRC *flag;

    const char *const event = "event: patch";

    const char *const body =
        "data: {\"path\": \"/flags/my-flag\", \"data\": "
        "{\"key\": \"my-flag\", \"version\": 3}}";

    LD_ASSERT(context);

    LD_ASSERT(LDi_onSSE(context, event));
    LD_ASSERT(LDi_onSSE(context, body));
    LD_ASSERT(LDi_onSSE(context, ""));

    LD_ASSERT(LDStoreGet(
        context->client->config->store, LD_FLAG, "my-flag", &flag));
    LD_ASSERT(flag);
    LD_ASSERT(LDGetNumber(LDObjectLookup(LDJSONRCGet(flag), "version")) == 3);

    LDJSONRCDecrement(flag);
}

static void
testDeleteFlag(struct StreamContext *const context)
{
    struct LDJSONRC *lookup;

    const char *const event = "event: delete";

    const char *const body =
        "data: {\"path\": \"/flags/my-flag\", \"version\": 4}";

    LD_ASSERT(context);

    LD_ASSERT(LDi_onSSE(context, event));
    LD_ASSERT(LDi_onSSE(context, body));
    LD_ASSERT(LDi_onSSE(context, ""));

    LD_ASSERT(LDStoreGet(
        context->client->config->store, LD_FLAG, "my-flag", &lookup));
    LD_ASSERT(!lookup);
}

static void
testPatchSegment(struct StreamContext *const context)
{
    struct LDJSONRC *segment;

    const char *const event = "event: patch";

    const char *const body =
        "data: {\"path\": \"/segments/my-segment\", \"data\": "
        "{\"key\": \"my-segment\", \"version\": 7}}";

    LD_ASSERT(context);

    LD_ASSERT(LDi_onSSE(context, event));
    LD_ASSERT(LDi_onSSE(context, body));
    LD_ASSERT(LDi_onSSE(context, ""));

    LD_ASSERT(LDStoreGet(
        context->client->config->store, LD_SEGMENT, "my-segment", &segment));
    LD_ASSERT(segment);
    LD_ASSERT(LDGetNumber(LDObjectLookup(LDJSONRCGet(segment), "version")) ==
        7);

    LDJSONRCDecrement(segment);
}

static void
testDeleteSegment(struct StreamContext *const context)
{
    struct LDJSONRC *lookup;

    const char *const event = "event: delete";

    const char *const body =
        "data: {\"path\": \"/segments/my-segment\", \"version\": 8}";

    LD_ASSERT(context);

    LD_ASSERT(LDi_onSSE(context, event));
    LD_ASSERT(LDi_onSSE(context, body));
    LD_ASSERT(LDi_onSSE(context, ""));

    LD_ASSERT(LDStoreGet(
        context->client->config->store, LD_SEGMENT, "my-segment", &lookup));
    LD_ASSERT(!lookup);
}

static void
testStreamOperations()
{
    struct LDConfig *config;
    struct LDClient *client;
    struct StreamContext *context;

    LD_ASSERT(config = LDConfigNew("key"));
    LD_ASSERT(client = LDClientInit(config, 0));
    LD_ASSERT(context = (struct StreamContext *)
        malloc(sizeof(struct StreamContext)));
    memset(context, 0, sizeof(struct StreamContext));
    context->client = client;

    testInitialPut(context);
    testPatchFlag(context);
    testDeleteFlag(context);
    testPatchSegment(context);
    testDeleteSegment(context);

    LDFree(context->dataBuffer);
    LDFree(context->memory);
    LDFree(context);
    LDClientClose(client);
}

int
main()
{
    LDConfigureGlobalLogger(LD_LOG_TRACE, LDBasicLogger);
    LDGlobalInit();

    testParsePathFlags();
    testParsePathSegments();
    testParsePathUnknownKind();
    testStreamOperations();

    return 0;
}
