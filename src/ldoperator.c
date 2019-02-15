#include "ldinternal.h"
#include "ldoperators.h"

#include <regex.h>

typedef bool (*OpFn)(const struct LDJSON *const uvalue,
    const struct LDJSON *const cvalue);

typedef bool (*StringOpFn)(const char *const uvalue, const char *const cvalue);

typedef bool (*NumberOpFn)(const float uvalue, const float cvalue);

#define CHECKSTRING(uvalue, cvalue) \
    if (LDJSONGetType(uvalue) != LDText || LDJSONGetType(cvalue) != LDText) { \
        return false; \
    }

bool
operatorInFn(const struct LDJSON *const uvalue,
    const struct LDJSON *const cvalue)
{
    if (LDJSONCompare(uvalue, cvalue)) {
        return true;
    }

    return false;
}

static bool
operatorStartsWithFn(const struct LDJSON *const uvalue,
    const struct LDJSON *const cvalue)
{
    size_t ulen;
    size_t clen;

    CHECKSTRING(uvalue, cvalue);

    ulen = strlen(LDGetText(uvalue));
    clen = strlen(LDGetText(cvalue));

    if (clen > ulen) {
        return false;
    }

    return strncmp(LDGetText(uvalue), LDGetText(cvalue), clen) == 0;
}

static bool
operatorEndsWithFn(const struct LDJSON *const uvalue,
    const struct LDJSON *const cvalue)
{
    size_t ulen;
    size_t clen;

    CHECKSTRING(uvalue, cvalue);

    ulen = strlen(LDGetText(uvalue));
    clen = strlen(LDGetText(cvalue));

    if (clen > ulen) {
        return false;
    }

    return strcmp(LDGetText(uvalue) + ulen - clen, LDGetText(cvalue)) == 0;
}

static bool
operatorMatchesFn(const struct LDJSON *const uvalue,
    const struct LDJSON *const cvalue)
{
    bool matches;
    regex_t *context = NULL;

    CHECKSTRING(uvalue, cvalue);

    if (regcomp(context, LDGetText(cvalue), 0) != 0) {
        LD_LOG(LD_LOG_ERROR, "failed to compile regex");

        return false;
    }

    matches = regexec(context, LDGetText(uvalue), 0, NULL, 0) == 0;

    regfree(context);

    return matches;
}

static bool
operatorContainsFn(const struct LDJSON *const uvalue,
    const struct LDJSON *const cvalue)
{
    CHECKSTRING(uvalue, cvalue);

    return strstr(LDGetText(uvalue), LDGetText(cvalue)) != NULL;
}

OpFn
lookupOperation(const char *const operation)
{
    LD_ASSERT(operation);

    if (strcmp(operation, "in") == 0) {
        return operatorInFn;
    } else if (strcmp(operation, "endsWith") == 0) {
        return operatorEndsWithFn;
    } else if (strcmp(operation, "startsWith") == 0) {
        return operatorStartsWithFn;
    } else if (strcmp(operation, "matches") == 0) {
        return operatorMatchesFn;
    } else if (strcmp(operation, "contains") == 0) {
        return operatorContainsFn;
    }

    return NULL;
}