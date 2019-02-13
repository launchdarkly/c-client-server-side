#include "sha1.h"
#include "hexify.h"

#include "ldinternal.h"
#include "ldevaluate.h"

static bool
addReason(struct LDJSON *result, const char *const reason)
{
    struct LDJSON *tmpcollection;
    struct LDJSON *tmp;

    LD_ASSERT(result);
    LD_ASSERT(reason);

    if (!(tmpcollection = LDNewObject())) {
        LD_LOG(LD_LOG_ERROR, "allocation error");

        return false;
    }

    if (!(tmp = LDNewText(reason))) {
        LD_LOG(LD_LOG_ERROR, "allocation error");

        return false;
    }

    if (!(LDObjectSetKey(tmpcollection, "kind", tmp))) {
        LD_LOG(LD_LOG_ERROR, "allocation error");

        return false;
    }

    if (!(LDObjectSetKey(result, "reason", tmpcollection))) {
        LD_LOG(LD_LOG_ERROR, "allocation error");

        return false;
    }

    return true;
}

static bool
addValue(const struct LDJSON *const flag, struct LDJSON *result,
    const struct LDJSON *const index)
{
    struct LDJSON *tmp;
    struct LDJSON *variations;
    struct LDJSON *variation;

    LD_ASSERT(flag);
    LD_ASSERT(result);

    if (index) {
        if (LDJSONGetType(index) != LDNumber) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return false;
        }

        if (!(tmp = LDNewNumber(LDGetNumber(index)))) {
            LD_LOG(LD_LOG_ERROR, "allocation error");

            return false;
        }
    } else {
        if (!(tmp = LDNewNull())) {
            LD_LOG(LD_LOG_ERROR, "allocation error");

            return false;
        }
    }

    if (!(LDObjectSetKey(result, "variationIndex", tmp))) {
        LD_LOG(LD_LOG_ERROR, "allocation error");

        return false;
    }

    if (index) {
        if (!(variations = LDObjectLookup(flag, "variations"))) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return false;
        }

        if (LDJSONGetType(variations) != LDArray) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return false;
        }

        if (!(variation = LDArrayLookup(variations,
            LDGetNumber(index))))
        {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return false;
        }

        if (!(tmp = LDJSONDuplicate(variation))) {
            LD_LOG(LD_LOG_ERROR, "allocation error");

            return false;
        }
    } else {
        if (!(tmp = LDNewNull())) {
            LD_LOG(LD_LOG_ERROR, "allocation error");

            return false;
        }
    }

    if (!(LDObjectSetKey(result, "value", tmp))) {
        LD_LOG(LD_LOG_ERROR, "allocation error");

        return false;
    }

    return true;
}

bool
evaluate(const struct LDJSON *const flag, const struct LDUser *const user,
    struct LDStore *const store, struct LDJSON **const result)
{
    LD_ASSERT(flag);
    LD_ASSERT(user);
    LD_ASSERT(store);
    LD_ASSERT(result);

    if (!(*result = LDNewObject())) {
        LD_LOG(LD_LOG_ERROR, "allocation error");

        return false;
    }

    if (LDJSONGetType(flag) != LDObject) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return false;
    }

    {
        const struct LDJSON *on = NULL;

        if (!(on = LDObjectLookup(flag, "on"))) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return false;
        }

        if (LDJSONGetType(on) != LDBool) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return false;
        }

        if (!LDGetBool(on)) {
            const struct LDJSON *offVariation =
                LDObjectLookup(flag, "offVariation");

            if (!addReason(*result, "OFF")) {
                LD_LOG(LD_LOG_ERROR, "failed to add reason");

                return false;
            }

            if (!(addValue(flag, *result, offVariation))) {
                LD_LOG(LD_LOG_ERROR, "failed to add value");

                return false;
            }

            return true;
        }
    }

    {
        bool submatch;

        if (!checkPrerequisites(flag, user, store, &submatch)) {
            LD_LOG(LD_LOG_ERROR, "sub error error");

            return false;
        }

        if (!submatch) {
            const struct LDJSON *offVariation =
                LDObjectLookup(flag, "offVariation");

            if (!addReason(*result, "PREREQUISITE_FAILED")) {
                LD_LOG(LD_LOG_ERROR, "failed to add reason");

                return false;
            }

            if (!(addValue(flag, *result, offVariation))) {
                LD_LOG(LD_LOG_ERROR, "failed to add value");

                return false;
            }

            return true;
        }
    }

    while (true) {
        const struct LDJSON *const targets = LDObjectLookup(flag, "targets");

        if (targets && LDJSONGetType(targets) != LDArray) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return false;
        }

        if (!targets) {
            break;
        }

        {
            const struct LDJSON *iter = NULL;

            for (iter = LDGetIter(targets); iter; iter = LDIterNext(iter)) {
                const struct LDJSON *values = NULL;

                LD_ASSERT(LDJSONGetType(iter) == LDObject);

                values = LDObjectLookup(iter, "values");

                LD_ASSERT(values); LD_ASSERT(LDJSONGetType(values) == LDArray);

                if (textInArray(values, user->key)) {
                    const struct LDJSON *variation = NULL;

                    if (!(variation = LDObjectLookup(iter, "variation"))) {
                        LD_LOG(LD_LOG_ERROR, "schema error");

                        return false;
                    }

                    if (!addReason(*result, "RULE_MATCH")) {
                        LD_LOG(LD_LOG_ERROR, "failed to add reason");

                        return false;
                    }

                    if (!(addValue(flag, *result, variation))) {
                        LD_LOG(LD_LOG_ERROR, "failed to add value");

                        return false;
                    }

                    return true;
                }
            }
        }

        break;
    }

    while (true) {
        const struct LDJSON *const rules = LDObjectLookup(flag, "rules");

        if (rules && LDJSONGetType(rules) != LDArray) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return false;
        }

        if (!rules || LDArrayGetSize(rules) == 0) {
            const struct LDJSON *fallthrough = NULL;

            if (!(fallthrough = LDObjectLookup(flag, "fallthrough"))) {
                LD_LOG(LD_LOG_ERROR, "schema error");

                return false;
            }

            fallthrough = LDObjectLookup(fallthrough, "variation");

            if (!addReason(*result, "FALLTHROUGH")) {
                LD_LOG(LD_LOG_ERROR, "failed to add reason");

                return false;
            }

            if (!(addValue(flag, *result, fallthrough))) {
                LD_LOG(LD_LOG_ERROR, "failed to add value");

                return false;
            }

            return true;
        }

        if (!rules) {
            break;
        }

        {
            const struct LDJSON *iter = NULL;

            for (iter = LDGetIter(rules); iter; iter = LDIterNext(iter)) {
                bool submatch;

                LD_ASSERT(LDJSONGetType(iter) == LDObject);

                if (!ruleMatchesUser(iter, user, &submatch)) {
                    LD_LOG(LD_LOG_ERROR, "sub error");

                    return false;
                }

                if (submatch) {
                    /* TODO return ruleMatch */

                    return true;
                }
            }
        }

        break;
    }

    return true;
}

bool
checkPrerequisites(const struct LDJSON *const flag,
    const struct LDUser *const user, struct LDStore *const store,
    bool *const matches)
{
    struct LDJSON *prerequisites = NULL;
    struct LDJSON *iter = NULL;

    LD_ASSERT(flag);
    LD_ASSERT(user);
    LD_ASSERT(store);
    LD_ASSERT(matches);

    if (LDJSONGetType(flag) != LDObject) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return false;
    }

    prerequisites = LDObjectLookup(flag, "prerequisites");

    if (!prerequisites) {
        *matches = true;

        return true;
    }

    if (LDJSONGetType(prerequisites) != LDArray) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return false;
    }

    for (iter = LDGetIter(prerequisites); iter; iter = LDIterNext(iter)) {
        struct LDJSON *result = NULL;
        struct LDJSON *preflag = NULL;
        const struct LDJSON *key = NULL;
        const struct LDJSON *variation = NULL;

        if (LDJSONGetType(iter) != LDObject) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return false;
        }

        if (!(key = LDObjectLookup(iter, "key"))) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return false;
        }

        if (LDJSONGetType(key) != LDText) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return false;
        }

        if (!(variation = LDObjectLookup(iter, "variation"))) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return false;
        }

        if (LDJSONGetType(variation) != LDNumber) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return false;
        }

        if (!(preflag = LDStoreGet(store, "flags", LDGetText(key)))) {
            LD_LOG(LD_LOG_ERROR, "store lookup error");

            return false;
        }

        if (!evaluate(preflag, user, store, &result)) {
            LDJSONFree(preflag);

            return false;
        }

        if (!result) {
            LDJSONFree(preflag);

            LD_LOG(LD_LOG_ERROR, "sub error with result");
        }

        {
            struct LDJSON *on;
            struct LDJSON *variationIndex;

            if (!(on = LDObjectLookup(preflag, "on"))) {
                LDJSONFree(preflag);
                LDJSONFree(result);

                LD_LOG(LD_LOG_ERROR, "schema error");

                return false;
            }

            if (LDJSONGetType(on) != LDBool) {
                LDJSONFree(preflag);
                LDJSONFree(result);

                LD_LOG(LD_LOG_ERROR, "schema error");

                return false;
            }

            if (!(variationIndex = LDObjectLookup(result, "variationIndex"))) {
                LDJSONFree(preflag);
                LDJSONFree(result);

                LD_LOG(LD_LOG_ERROR, "schema error");

                return false;
            }

            if (LDJSONGetType(variationIndex) != LDNumber) {
                LDJSONFree(preflag);
                LDJSONFree(result);

                LD_LOG(LD_LOG_ERROR, "schema error");

                return false;
            }

            if (!LDGetBool(on) ||
                LDGetNumber(variationIndex) != LDGetNumber(variation))
            {
                LDJSONFree(preflag);
                LDJSONFree(result);

                *matches = false;

                return true;
            }
        }

        LDJSONFree(preflag);
        LDJSONFree(result);
    }

    *matches = true;

    return true;
}

bool
ruleMatchesUser(const struct LDJSON *const rule,
    const struct LDUser *const user, bool *const matches)
{
    const struct LDJSON *clauses = NULL;
    const struct LDJSON *iter = NULL;

    LD_ASSERT(rule);
    LD_ASSERT(user);
    LD_ASSERT(matches);

    if (!(clauses = LDObjectLookup(rule, "clauses"))) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return false;
    }

    if (LDJSONGetType(clauses) != LDArray) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return false;
    }

    for (iter = LDGetIter(clauses); iter; iter = LDIterNext(iter)) {
        bool submatch;

        if (LDJSONGetType(iter) != LDObject) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return false;
        }

        if (!clauseMatchesUser(iter, user, &submatch)) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return false;
        }

        if (!submatch) {
            *matches = false;

            return true;
        }
    }

    *matches = true;

    return true;
}

bool
clauseMatchesUser(const struct LDJSON *const clause,
    const struct LDUser *const user, bool *const matches)
{
    const struct LDJSON *op = NULL;

    LD_ASSERT(clause);
    LD_ASSERT(user);

    if (LDJSONGetType(clause) != LDObject) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return false;
    }

    if (!(op = LDObjectLookup(clause, "op"))) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return false;
    }

    if (LDJSONGetType(op) != LDText) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return false;
    }

    if (strcmp(LDGetText(op), "segmentMatch") == 0) {
        const struct LDJSON *values = NULL;
        const struct LDJSON *iter = NULL;
        const struct LDJSON *negate = NULL;

        if (!(values = LDObjectLookup(clause, "values"))) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return false;
        }

        if (LDJSONGetType(values) != LDArray) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return false;
        }

        if (!(negate = LDObjectLookup(clause, "negate"))) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return false;
        }

        if (LDJSONGetType(negate) != LDBool) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return false;
        }

        for (iter = LDGetIter(values); iter; iter = LDIterNext(iter)) {
            if (LDJSONGetType(iter) == LDText) {
                const struct LDJSON *const segment = NULL;
                bool submatch;

                if (!segmentMatchesUser(segment, user, &submatch)) {
                    LD_LOG(LD_LOG_ERROR, "sub error");

                    return false;
                }

                if (submatch) {
                    *matches = LDGetBool(negate) ? false : true;

                    return true;
                }
            }
        }

        *matches = LDGetBool(negate) ? true : true;

        return true;
    }

    {
        bool submatch;

        if (!clauseMatchesUserNoSegments(clause, user, &submatch)) {
            LD_LOG(LD_LOG_ERROR, "sub error");

            return false;
        }

        *matches = submatch;

        return true;
    }
}

bool
segmentMatchesUser(const struct LDJSON *const segment,
    const struct LDUser *const user, bool *const matches)
{
    const struct LDJSON *included = NULL;
    const struct LDJSON *excluded = NULL;

    LD_ASSERT(segment);
    LD_ASSERT(user);

    if (!(included = LDObjectLookup(segment, "included"))) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return false;
    }

    if (LDJSONGetType(included) != LDArray) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return false;
    }

    if (textInArray(included, user->key)) {
        *matches = true;

        return true;
    } else if (textInArray(excluded, user->key)) {
        *matches = false;

        return true;
    } else {
        const struct LDJSON *segmentRules = NULL;
        const struct LDJSON *iter = NULL;

        if (!(segmentRules = LDObjectLookup(segment, "included"))) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return false;
        }

        if (LDJSONGetType(segmentRules) != LDArray) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return false;
        }

        for (iter = LDGetIter(segmentRules); iter; iter = LDIterNext(iter)) {
            const struct LDJSON *key = NULL;
            const struct LDJSON *salt = NULL;
            bool submatches;

            if (LDJSONGetType(iter) != LDObject) {
                LD_LOG(LD_LOG_ERROR, "schema error");

                return false;
            }

            if (!(key = LDObjectLookup(iter, "key"))) {
                LD_LOG(LD_LOG_ERROR, "schema error");

                return false;
            }

            if (LDJSONGetType(key) != LDText) {
                LD_LOG(LD_LOG_ERROR, "schema error");

                return false;
            }

            if (!(salt = LDObjectLookup(iter, "salt"))) {
                LD_LOG(LD_LOG_ERROR, "schema error");

                return false;
            }

            if (LDJSONGetType(salt) != LDText) {
                LD_LOG(LD_LOG_ERROR, "schema error");

                return false;
            }

            if (!segmentRuleMatchUser(iter, LDGetText(key), user,
                LDGetText(salt), &submatches))
            {
                return false;
            }

            if (submatches) {
                *matches = submatches;

                return true;
            }
        }

        *matches = false;

        return true;
    }
}

bool
segmentRuleMatchUser(const struct LDJSON *const segmentRule,
    const char *const segmentKey, const struct LDUser *const user,
    const char *const salt, bool *const matches)
{
    const struct LDJSON *clauses = NULL;
    const struct LDJSON *clause = NULL;

    LD_ASSERT(segmentRule);
    LD_ASSERT(segmentKey);
    LD_ASSERT(user);
    LD_ASSERT(salt);
    LD_ASSERT(matches);

    if (!(clauses = LDObjectLookup(segmentRule, "clauses"))) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return false;
    }

    if (LDJSONGetType(clauses) != LDArray) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return false;
    }

    for (clause = LDGetIter(clauses); clause; clause = LDIterNext(clause)) {
        bool submatches;

        if (!clauseMatchesUserNoSegments(clause, user, &submatches)) {
            return false;
        }

        if (!submatches) {
            *matches = false;

            return true;
        }
    }

    {
        const struct LDJSON *weight = LDObjectLookup(segmentRule, "weight");

        if (!weight) {
            return true;
        }

        if (LDJSONGetType(weight) != LDNumber) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return false;
        }

        {
            float bucket;

            const struct LDJSON *bucketBy =
                LDObjectLookup(segmentRule, "bucketBy");

            const char *const attribute = (bucketBy == NULL)
                ? "key" : LDGetText(bucketBy);

            if (!bucketUser(user, segmentKey, attribute, salt, &bucket)) {
                LD_LOG(LD_LOG_ERROR, "bucketUser error");

                return false;
            }

            *matches = bucket < LDGetNumber(weight) / 100000;

            return true;
        }
    }
}

bool
clauseMatchesUserNoSegments(const struct LDJSON *const clause,
    const struct LDUser *const user, bool *const matches)
{
    const char *attributeText = NULL;
    struct LDJSON *attributeValue = NULL;
    struct LDJSON *attribute = NULL;
    LDJSONType type;

    LD_ASSERT(clause);
    LD_ASSERT(user);

    if (!(attribute = LDObjectLookup(clause, "attribute"))) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return false;
    }

    if (LDJSONGetType(attribute) != LDText) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return false;
    }

    if (!(attributeText = LDGetText(attribute))) {
        LD_LOG(LD_LOG_ERROR, "allocation error");

        return false;
    }

    if (!(attributeValue = valueOfAttribute(user, attributeText))) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return false;
    }

    if ((type = LDJSONGetType(attributeValue)) == LDArray) {
        const struct LDJSON *negate = NULL;
        struct LDJSON *iter = LDGetIter(attributeValue);

        for (; iter; iter = LDIterNext(iter)) {
            type = LDJSONGetType(iter);

            if (type == LDObject || type == LDArray) {
                LD_LOG(LD_LOG_ERROR, "schema error");

                LDJSONFree(attributeValue);

                return false;
            }
        }

        LDJSONFree(attributeValue);

        if (!(negate = LDObjectLookup(clause, "negate"))) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return false;
        }

        if (LDJSONGetType(negate) != LDBool) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return false;
        }

        *matches = LDGetBool(negate) ? false : true;

        return true;
    } else if (type != LDObject && type != LDArray) {

        /* match any */
        LDJSONFree(attributeValue);

        return true;
    } else {
        LDJSONFree(attributeValue);

        return false;
    }
}

bool
bucketUser(const struct LDUser *const user, const char *const segmentKey,
    const char *const attribute, const char *const salt, float *const bucket)
{
    struct LDJSON *attributeValue = NULL;

    LD_ASSERT(user);
    LD_ASSERT(segmentKey);
    LD_ASSERT(attribute);
    LD_ASSERT(salt);
    LD_ASSERT(bucket);

    if ((attributeValue = valueOfAttribute(user, attribute))) {
        char raw[256];

        char *const bucketable = bucketableStringValue(attributeValue);

        LDJSONFree(attributeValue);

        if (!bucketable) {
            return false;
        }

        if (snprintf(raw, sizeof(raw), "%s.%s.%s", segmentKey,
            salt, bucketable) >= 0)
        {
            char digest[20];
            char encoded[17];
            float longScale = 0xFFFFFFFFFFFFFFF;

            SHA1(digest, raw, strlen(raw));

            /* encodes to hex, and shortens, 16 characters in hex 8 bytes */
            LD_ASSERT(hexify((unsigned char *)digest,
                sizeof(digest), encoded, sizeof(encoded)) == 16);

            encoded[15] = 0;

            *bucket = (float)strtoll(encoded, NULL, 16) / longScale;

            free(bucketable);

            return true;
        }
    }

    return false;
}

char *
bucketableStringValue(const struct LDJSON *const node)
{
    LD_ASSERT(node);

    if (LDJSONGetType(node) == LDText) {
        return strdup(LDGetText(node));
    } else if (LDJSONGetType(node) == LDNumber) {
        char buffer[256];

        if (snprintf(buffer, sizeof(buffer), "%f", LDGetNumber(node)) < 0) {
            return NULL;
        } else {
            return strdup(buffer);
        }
    } else {
        return NULL;
    }
}
