#include <string.h>

#include "hexify.h"
#include "sha1.h"

#include <launchdarkly/api.h>

#include "assertion.h"
#include "client.h"
#include "evaluate.h"
#include "event_processor.h"
#include "network.h"
#include "operators.h"
#include "store.h"
#include "user.h"
#include "utility.h"

LDBoolean
LDi_isEvalError(const EvalStatus status)
{
    return status == EVAL_MEM || status == EVAL_SCHEMA || status == EVAL_STORE;
}

static EvalStatus
maybeNegate(const struct LDJSON *const clause, const EvalStatus status)
{
    const struct LDJSON *negate;

    LD_ASSERT(clause);

    negate = NULL;

    if (LDi_isEvalError(status)) {
        return status;
    }

    if (LDi_notNull(negate = LDObjectLookup(clause, "negate"))) {
        if (LDJSONGetType(negate) != LDBool) {
            return EVAL_SCHEMA;
        }

        if (LDGetBool(negate)) {
            if (status == EVAL_MATCH) {
                return EVAL_MISS;
            } else if (status == EVAL_MISS) {
                return EVAL_MATCH;
            }
        }
    }

    return status;
}

static LDBoolean
addValue(
    const struct LDJSON *const flag,
    struct LDJSON **           result,
    struct LDDetails *const    details,
    const struct LDJSON *const index)
{
    struct LDJSON *tmp, *variations, *variation;

    LD_ASSERT(flag);
    LD_ASSERT(result);
    LD_ASSERT(details);

    tmp        = NULL;
    variations = NULL;
    variation  = NULL;

    if (LDi_notNull(index)) {
        details->hasVariation = LDBooleanTrue;

        if (LDJSONGetType(index) != LDNumber) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return LDBooleanFalse;
        }

        details->variationIndex = LDGetNumber(index);

        if (!(variations = LDObjectLookup(flag, "variations"))) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return LDBooleanFalse;
        }

        if (LDJSONGetType(variations) != LDArray) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return LDBooleanFalse;
        }

        if (!(variation = LDArrayLookup(variations, LDGetNumber(index)))) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return LDBooleanFalse;
        }

        if (!(tmp = LDJSONDuplicate(variation))) {
            LD_LOG(LD_LOG_ERROR, "allocation error");

            return LDBooleanFalse;
        }

        *result = tmp;
    } else {
        *result               = NULL;
        details->hasVariation = LDBooleanFalse;
    }

    return LDBooleanTrue;
}

static const char *
LDi_getBucketAttribute(const struct LDJSON *const obj)
{
    const struct LDJSON *bucketBy;

    LD_ASSERT(obj);
    LD_ASSERT(LDJSONGetType(obj) == LDObject);

    bucketBy = LDObjectLookup(obj, "bucketBy");

    if (!LDi_notNull(bucketBy)) {
        return "key";
    }

    if (LDJSONGetType(bucketBy) != LDText) {
        return NULL;
    }

    return LDGetText(bucketBy);
}

EvalStatus
LDi_evaluate(
    struct LDClient *const     client,
    const struct LDJSON *const flag,
    const struct LDUser *const user,
    struct LDStore *const      store,
    struct LDDetails *const    details,
    struct LDJSON **const      o_events,
    struct LDJSON **const      o_value,
    const LDBoolean            recordReason)
{
    EvalStatus           substatus;
    const struct LDJSON *iter, *rules, *targets, *on;
    const struct LDJSON *index;
    const char *         failedKey;
    LDBoolean            inExperiment;

    LD_ASSERT(flag);
    LD_ASSERT(user);
    LD_ASSERT(store);
    LD_ASSERT(details);
    LD_ASSERT(o_events);
    LD_ASSERT(o_value);

    iter      = NULL;
    rules     = NULL;
    targets   = NULL;
    on        = NULL;
    failedKey = NULL;
    index     = NULL;

    if (LDJSONGetType(flag) != LDObject) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return EVAL_SCHEMA;
    }

    /* on */
    if (!(on = LDObjectLookup(flag, "on"))) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return EVAL_SCHEMA;
    }

    if (LDJSONGetType(on) != LDBool) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return EVAL_SCHEMA;
    }

    if (!LDGetBool(on)) {
        const struct LDJSON *offVariation;

        offVariation = LDObjectLookup(flag, "offVariation");

        details->reason = LD_OFF;

        if (!(addValue(flag, o_value, details, offVariation))) {
            LD_LOG(LD_LOG_ERROR, "failed to add value");

            return EVAL_MEM;
        }

        return EVAL_MISS;
    }

    /* prerequisites */
    if (LDi_isEvalError(
            substatus = LDi_checkPrerequisites(
                client, flag, user, store, &failedKey, o_events, recordReason)))
    {
        LD_LOG(LD_LOG_ERROR, "checkPrequisites failed");

        return substatus;
    }

    if (substatus == EVAL_MISS) {
        char *key;

        if (!(key = LDStrDup(failedKey))) {
            LD_LOG(LD_LOG_ERROR, "memory error");

            return EVAL_MEM;
        }

        details->reason                = LD_PREREQUISITE_FAILED;
        details->extra.prerequisiteKey = key;

        if (!(addValue(
                flag, o_value, details, LDObjectLookup(flag, "offVariation"))))
        {
            LD_LOG(LD_LOG_ERROR, "failed to add value");

            return EVAL_MEM;
        }

        return EVAL_MISS;
    }

    /* targets */
    targets = LDObjectLookup(flag, "targets");

    if (targets && LDJSONGetType(targets) != LDArray) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return EVAL_SCHEMA;
    }

    if (targets) {
        for (iter = LDGetIter(targets); iter; iter = LDIterNext(iter)) {
            const struct LDJSON *values = NULL;

            if (LDJSONGetType(iter) != LDObject) {
                LD_LOG(LD_LOG_ERROR, "schema error");

                return EVAL_SCHEMA;
            }

            if (!(values = LDObjectLookup(iter, "values"))) {
                LD_LOG(LD_LOG_ERROR, "schema error");

                return EVAL_SCHEMA;
            }

            if (LDJSONGetType(values) != LDArray) {
                LD_LOG(LD_LOG_ERROR, "schema error");

                return EVAL_SCHEMA;
            }

            if (LDi_textInArray(values, user->key)) {
                const struct LDJSON *variation = NULL;

                variation = LDObjectLookup(iter, "variation");

                details->reason = LD_TARGET_MATCH;

                if (!(addValue(flag, o_value, details, variation))) {
                    LD_LOG(LD_LOG_ERROR, "failed to add value");

                    return EVAL_MEM;
                }

                return EVAL_MATCH;
            }
        }
    }

    /* rules */
    rules = LDObjectLookup(flag, "rules");

    if (rules && LDJSONGetType(rules) != LDArray) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return EVAL_SCHEMA;
    }

    if (rules) {
        unsigned int index;

        index = 0;

        for (iter = LDGetIter(rules); iter; iter = LDIterNext(iter)) {
            EvalStatus substatus;

            if (LDJSONGetType(iter) != LDObject) {
                LD_LOG(LD_LOG_ERROR, "schema error");

                return EVAL_SCHEMA;
            }

            if (LDi_isEvalError(
                    substatus = LDi_ruleMatchesUser(iter, user, store))) {
                LD_LOG(LD_LOG_ERROR, "sub error");

                return substatus;
            }

            if (substatus == EVAL_MATCH) {
                struct LDJSON *      ruleid;
                const struct LDJSON *variation;

                variation = NULL;
                ruleid    = NULL;

                details->reason               = LD_RULE_MATCH;
                details->extra.rule.ruleIndex = index;
                details->extra.rule.id        = NULL;

                if (!LDi_getIndexForVariationOrRollout(
                        flag, iter, user, &inExperiment, &variation))
                {
                    LD_LOG(LD_LOG_ERROR, "schema error");

                    return EVAL_SCHEMA;
                }

                details->extra.rule.inExperiment = inExperiment;

                if (!(addValue(flag, o_value, details, variation))) {
                    LD_LOG(LD_LOG_ERROR, "failed to add value");

                    return EVAL_MEM;
                }

                if (LDi_notNull(ruleid = LDObjectLookup(iter, "id"))) {
                    char *text;

                    if (LDJSONGetType(ruleid) != LDText) {
                        LD_LOG(LD_LOG_ERROR, "schema error");

                        return EVAL_SCHEMA;
                    }

                    if (!(text = LDStrDup(LDGetText(ruleid)))) {
                        LD_LOG(LD_LOG_ERROR, "memory error");

                        return EVAL_MEM;
                    }

                    details->extra.rule.id = text;
                }

                return EVAL_MATCH;
            }

            index++;
        }
    }

    /* fallthrough */
    details->reason = LD_FALLTHROUGH;

    if (!LDi_getIndexForVariationOrRollout(
            flag,
            LDObjectLookup(flag, "fallthrough"),
            user,
            &inExperiment,
            &index))
    {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return EVAL_SCHEMA;
    }

    details->extra.fallthrough.inExperiment = inExperiment;

    if (!(addValue(flag, o_value, details, index))) {
        LD_LOG(LD_LOG_ERROR, "failed to add value");

        return EVAL_MEM;
    }

    return EVAL_MATCH;
}

EvalStatus
LDi_checkPrerequisites(
    struct LDClient *const     client,
    const struct LDJSON *const flag,
    const struct LDUser *const user,
    struct LDStore *const      store,
    const char **const         failedKey,
    struct LDJSON **const      events,
    const LDBoolean            recordReason)
{
    struct LDJSON *prerequisites, *iter;

    LD_ASSERT(flag);
    LD_ASSERT(user);
    LD_ASSERT(store);
    LD_ASSERT(failedKey);
    LD_ASSERT(events);
    LD_ASSERT(LDJSONGetType(flag) == LDObject);

    prerequisites = NULL;
    iter          = NULL;

    prerequisites = LDObjectLookup(flag, "prerequisites");

    if (!prerequisites) {
        return EVAL_MATCH;
    }

    if (LDJSONGetType(prerequisites) != LDArray) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return EVAL_SCHEMA;
    }

    for (iter = LDGetIter(prerequisites); iter; iter = LDIterNext(iter)) {
        struct LDJSON *      value, *preflag, *event, *subevents;
        const struct LDJSON *key, *variation;
        unsigned int *       variationNumRef;
        EvalStatus           status;
        const char *         keyText;
        struct LDDetails     details;
        struct LDJSONRC *    preflagrc;
        double               now;

        value           = NULL;
        preflag         = NULL;
        key             = NULL;
        variation       = NULL;
        variationNumRef = NULL;
        event           = NULL;
        subevents       = NULL;
        keyText         = NULL;
        preflagrc       = NULL;

        LDDetailsInit(&details);
        LDi_getUnixMilliseconds(&now);

        if (LDJSONGetType(iter) != LDObject) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return EVAL_SCHEMA;
        }

        if (!(key = LDObjectLookup(iter, "key"))) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return EVAL_SCHEMA;
        }

        if (LDJSONGetType(key) != LDText) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return EVAL_SCHEMA;
        }

        keyText = LDGetText(key);

        *failedKey = keyText;

        if (!(variation = LDObjectLookup(iter, "variation"))) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return EVAL_SCHEMA;
        }

        if (LDJSONGetType(variation) != LDNumber) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return EVAL_SCHEMA;
        }

        if (!LDStoreGet(store, LD_FLAG, keyText, &preflagrc)) {
            LD_LOG(LD_LOG_ERROR, "store lookup error");

            return EVAL_STORE;
        }

        if (preflagrc) {
            preflag = LDJSONRCGet(preflagrc);
        }

        if (!preflag) {
            LD_LOG(LD_LOG_ERROR, "cannot find flag in store");

            return EVAL_MISS;
        }

        if (LDi_isEvalError(
                status = LDi_evaluate(
                    client,
                    preflag,
                    user,
                    store,
                    &details,
                    &subevents,
                    &value,
                    recordReason)))
        {
            LDJSONRCDecrement(preflagrc);
            LDJSONFree(value);
            LDDetailsClear(&details);
            LDJSONFree(subevents);

            return status;
        }

        if (!value) {
            LD_LOG(LD_LOG_ERROR, "sub error with result");
        }

        if (details.hasVariation) {
            variationNumRef = &details.variationIndex;
        }

        event = LDi_newFeatureRequestEvent(
            client->eventProcessor,
            keyText,
            user,
            variationNumRef,
            value,
            NULL,
            LDGetText(LDObjectLookup(flag, "key")),
            preflag,
            &details,
            now);

        if (!event) {
            LDJSONRCDecrement(preflagrc);
            LDJSONFree(value);
            LDDetailsClear(&details);
            LDJSONFree(subevents);

            LD_LOG(LD_LOG_ERROR, "alloc error");

            return EVAL_MEM;
        }

        if (!(*events)) {
            if (!(*events = LDNewArray())) {
                LDJSONRCDecrement(preflagrc);
                LDJSONFree(value);
                LDDetailsClear(&details);
                LDJSONFree(subevents);

                LD_LOG(LD_LOG_ERROR, "alloc error");

                return EVAL_MEM;
            }
        }

        if (subevents) {
            if (!LDArrayAppend(*events, subevents)) {
                LDJSONRCDecrement(preflagrc);
                LDJSONFree(value);
                LDDetailsClear(&details);
                LDJSONFree(subevents);

                LD_LOG(LD_LOG_ERROR, "alloc error");

                return EVAL_MEM;
            }

            LDJSONFree(subevents);
        }

        if (!LDArrayPush(*events, event)) {
            LDJSONRCDecrement(preflagrc);
            LDJSONFree(value);
            LDDetailsClear(&details);

            LD_LOG(LD_LOG_ERROR, "alloc error");

            return EVAL_MEM;
        }

        if (status == EVAL_MISS) {
            LDJSONRCDecrement(preflagrc);
            LDJSONFree(value);
            LDDetailsClear(&details);

            return EVAL_MISS;
        }

        {
            struct LDJSON *on;
            LDBoolean      variationMatch = LDBooleanFalse;

            if (!(on = LDObjectLookup(preflag, "on"))) {
                LDJSONRCDecrement(preflagrc);
                LDJSONFree(value);
                LDDetailsClear(&details);

                LD_LOG(LD_LOG_ERROR, "schema error");

                return EVAL_SCHEMA;
            }

            if (LDJSONGetType(on) != LDBool) {
                LDJSONRCDecrement(preflagrc);
                LDJSONFree(value);
                LDDetailsClear(&details);

                LD_LOG(LD_LOG_ERROR, "schema error");

                return EVAL_SCHEMA;
            }

            if (details.hasVariation) {
                variationMatch =
                    details.variationIndex == LDGetNumber(variation);
            }

            if (!LDGetBool(on) || !variationMatch) {
                LDJSONRCDecrement(preflagrc);
                LDJSONFree(value);
                LDDetailsClear(&details);

                return EVAL_MISS;
            }
        }

        LDJSONRCDecrement(preflagrc);
        LDJSONFree(value);
        LDDetailsClear(&details);
    }

    return EVAL_MATCH;
}

EvalStatus
LDi_ruleMatchesUser(
    const struct LDJSON *const rule,
    const struct LDUser *const user,
    struct LDStore *const      store)
{
    const struct LDJSON *clauses = NULL;
    const struct LDJSON *iter    = NULL;

    LD_ASSERT(rule);
    LD_ASSERT(user);

    if (!(clauses = LDObjectLookup(rule, "clauses"))) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return EVAL_SCHEMA;
    }

    if (LDJSONGetType(clauses) != LDArray) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return EVAL_SCHEMA;
    }

    for (iter = LDGetIter(clauses); iter; iter = LDIterNext(iter)) {
        EvalStatus substatus;

        if (LDJSONGetType(iter) != LDObject) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return EVAL_SCHEMA;
        }

        if (LDi_isEvalError(
                substatus = LDi_clauseMatchesUser(iter, user, store))) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return substatus;
        }

        if (substatus == EVAL_MISS) {
            return EVAL_MISS;
        }
    }

    return EVAL_MATCH;
}

EvalStatus
LDi_clauseMatchesUser(
    const struct LDJSON *const clause,
    const struct LDUser *const user,
    struct LDStore *const      store)
{
    const struct LDJSON *op;

    LD_ASSERT(clause);
    LD_ASSERT(user);

    op = NULL;

    if (LDJSONGetType(clause) != LDObject) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return EVAL_SCHEMA;
    }

    if (!(op = LDObjectLookup(clause, "op"))) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return EVAL_SCHEMA;
    }

    if (LDJSONGetType(op) != LDText) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return EVAL_SCHEMA;
    }

    if (strcmp(LDGetText(op), "segmentMatch") == 0) {
        const struct LDJSON *values, *iter;

        values = NULL;
        iter   = NULL;

        if (!(values = LDObjectLookup(clause, "values"))) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return EVAL_SCHEMA;
        }

        if (LDJSONGetType(values) != LDArray) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return EVAL_SCHEMA;
        }

        for (iter = LDGetIter(values); iter; iter = LDIterNext(iter)) {
            if (LDJSONGetType(iter) == LDText) {
                EvalStatus       evalstatus;
                struct LDJSON *  segment;
                struct LDJSONRC *segmentrc;

                segmentrc = NULL;
                segment   = NULL;

                if (!LDStoreGet(store, LD_SEGMENT, LDGetText(iter), &segmentrc))
                {
                    LD_LOG(LD_LOG_ERROR, "store lookup error");

                    return EVAL_STORE;
                }

                if (segmentrc) {
                    segment = LDJSONRCGet(segmentrc);
                }

                if (!segment) {
                    LD_LOG(LD_LOG_WARNING, "segment not found in store");

                    continue;
                }

                if (LDi_isEvalError(
                        evalstatus = LDi_segmentMatchesUser(segment, user))) {
                    LD_LOG(LD_LOG_ERROR, "sub error");

                    LDJSONRCDecrement(segmentrc);

                    return evalstatus;
                }

                LDJSONRCDecrement(segmentrc);

                if (evalstatus == EVAL_MATCH) {
                    return maybeNegate(clause, EVAL_MATCH);
                }
            }
        }

        return maybeNegate(clause, EVAL_MISS);
    }

    return LDi_clauseMatchesUserNoSegments(clause, user);
}

EvalStatus
LDi_segmentMatchesUser(
    const struct LDJSON *const segment, const struct LDUser *const user)
{
    const struct LDJSON *included, *excluded, *iter, *salt, *segmentRules, *key;

    LD_ASSERT(segment);
    LD_ASSERT(user);

    included     = NULL;
    excluded     = NULL;
    key          = NULL;
    salt         = NULL;
    segmentRules = NULL;
    iter         = NULL;

    included = LDObjectLookup(segment, "included");

    if (LDi_notNull(included)) {
        if (LDJSONGetType(included) != LDArray) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return EVAL_SCHEMA;
        }

        if (LDi_textInArray(included, user->key)) {
            return EVAL_MATCH;
        }
    }

    excluded = LDObjectLookup(segment, "excluded");

    if (LDi_notNull(excluded)) {
        if (LDJSONGetType(excluded) != LDArray) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return EVAL_SCHEMA;
        }

        if (LDi_textInArray(excluded, user->key)) {
            return EVAL_MISS;
        }
    }

    if (!(segmentRules = LDObjectLookup(segment, "rules"))) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return EVAL_SCHEMA;
    }

    if (LDJSONGetType(segmentRules) != LDArray) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return EVAL_SCHEMA;
    }

    if (!(key = LDObjectLookup(segment, "key"))) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return EVAL_SCHEMA;
    }

    if (LDJSONGetType(key) != LDText) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return EVAL_SCHEMA;
    }

    if (!(salt = LDObjectLookup(segment, "salt"))) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return EVAL_SCHEMA;
    }

    if (LDJSONGetType(salt) != LDText) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return EVAL_SCHEMA;
    }

    for (iter = LDGetIter(segmentRules); iter; iter = LDIterNext(iter)) {
        EvalStatus substatus;

        if (LDJSONGetType(iter) != LDObject) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return EVAL_SCHEMA;
        }

        if (LDi_isEvalError(
                substatus = LDi_segmentRuleMatchUser(
                    iter, LDGetText(key), user, LDGetText(salt))))
        {
            return substatus;
        }

        if (substatus == EVAL_MATCH) {
            return EVAL_MATCH;
        }
    }

    return EVAL_MISS;
}

EvalStatus
LDi_segmentRuleMatchUser(
    const struct LDJSON *const segmentRule,
    const char *const          segmentKey,
    const struct LDUser *const user,
    const char *const          salt)
{
    const struct LDJSON *clauses, *clause;

    LD_ASSERT(segmentRule);
    LD_ASSERT(segmentKey);
    LD_ASSERT(user);
    LD_ASSERT(salt);

    clauses = NULL;
    clause  = NULL;

    if (!(clauses = LDObjectLookup(segmentRule, "clauses"))) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return EVAL_SCHEMA;
    }

    if (LDJSONGetType(clauses) != LDArray) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return EVAL_SCHEMA;
    }

    for (clause = LDGetIter(clauses); clause; clause = LDIterNext(clause)) {
        EvalStatus substatus;

        if (LDi_isEvalError(
                substatus = LDi_clauseMatchesUserNoSegments(clause, user))) {
            return substatus;
        }

        if (substatus == EVAL_MISS) {
            return EVAL_MISS;
        }
    }

    {
        const struct LDJSON *weight = LDObjectLookup(segmentRule, "weight");

        if (!LDi_notNull(weight)) {
            return EVAL_MATCH;
        }

        if (LDJSONGetType(weight) != LDNumber) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return EVAL_SCHEMA;
        }

        {
            float       bucket;
            const char *attribute;

            attribute = LDi_getBucketAttribute(segmentRule);

            if (attribute == NULL) {
                LD_LOG(LD_LOG_ERROR, "failed to parse bucketBy");

                return EVAL_SCHEMA;
            }

            LDi_bucketUser(user, segmentKey, attribute, salt, NULL, &bucket);

            if (bucket < LDGetNumber(weight) / 100000) {
                return EVAL_MATCH;
            } else {
                return EVAL_MISS;
            }
        }
    }
}

static EvalStatus
matchAny(
    OpFn f, const struct LDJSON *const value, const struct LDJSON *const values)
{
    const struct LDJSON *iter;

    LD_ASSERT(f);
    LD_ASSERT(value);
    LD_ASSERT(values);

    for (iter = LDGetIter(values); iter; iter = LDIterNext(iter)) {
        if (f(value, iter)) {
            return EVAL_MATCH;
        }
    }

    return EVAL_MISS;
}

EvalStatus
LDi_clauseMatchesUserNoSegments(
    const struct LDJSON *const clause, const struct LDUser *const user)
{
    OpFn                 fn;
    const char *         operatorText, *attributeText;
    struct LDJSON *      operatorJSON, *attributeValue, *attribute;
    const struct LDJSON *values;
    LDJSONType           type;

    LD_ASSERT(clause);
    LD_ASSERT(user);

    fn             = NULL;
    operatorText   = NULL;
    operatorJSON   = NULL;
    attributeText  = NULL;
    attributeValue = NULL;
    attribute      = NULL;
    values         = NULL;

    if (!(attribute = LDObjectLookup(clause, "attribute"))) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return EVAL_SCHEMA;
    }

    if (LDJSONGetType(attribute) != LDText) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return EVAL_SCHEMA;
    }

    if (!(attributeText = LDGetText(attribute))) {
        LD_LOG(LD_LOG_ERROR, "allocation error");

        return EVAL_SCHEMA;
    }

    if (!(operatorJSON = LDObjectLookup(clause, "op"))) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return EVAL_SCHEMA;
    }

    if (LDJSONGetType(operatorJSON) != LDText) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return EVAL_SCHEMA;
    }

    if (!(operatorText = LDGetText(operatorJSON))) {
        LD_LOG(LD_LOG_ERROR, "allocation error");

        return EVAL_SCHEMA;
    }

    if (!(values = LDObjectLookup(clause, "values"))) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return EVAL_SCHEMA;
    }

    if (!(fn = LDi_lookupOperation(operatorText))) {
        LD_LOG(LD_LOG_WARNING, "unknown operator");

        return EVAL_MISS;
    }

    if (!(attributeValue = LDi_valueOfAttribute(user, attributeText))) {
        LD_LOG(LD_LOG_TRACE, "attribute does not exist");

        return EVAL_MISS;
    }

    type = LDJSONGetType(attributeValue);

    if (type == LDArray) {
        struct LDJSON *iter;

        for (iter = LDGetIter(attributeValue); iter; iter = LDIterNext(iter)) {
            EvalStatus substatus;

            type = LDJSONGetType(iter);

            if (type == LDObject || type == LDArray) {
                LD_LOG(LD_LOG_ERROR, "schema error");

                LDJSONFree(attributeValue);

                return EVAL_SCHEMA;
            }

            if (LDi_isEvalError(substatus = matchAny(fn, iter, values))) {
                LD_LOG(LD_LOG_ERROR, "sub error");

                LDJSONFree(attributeValue);

                return substatus;
            }

            if (substatus == EVAL_MATCH) {
                LDJSONFree(attributeValue);

                return maybeNegate(clause, EVAL_MATCH);
            }
        }

        LDJSONFree(attributeValue);

        return maybeNegate(clause, EVAL_MISS);
    } else {
        EvalStatus substatus;

        if (LDi_isEvalError(substatus = matchAny(fn, attributeValue, values))) {
            LD_LOG(LD_LOG_ERROR, "sub error");

            LDJSONFree(attributeValue);

            return substatus;
        }

        LDJSONFree(attributeValue);

        return maybeNegate(clause, substatus);
    }
}

static float
LDi_hexToDecimal(const char *const input)
{
    float       acc;
    const char *i;

    LD_ASSERT(input);

    acc = 0;

    for (i = input; *i != '\0'; i++) {
        char charOffset;

        if (*i >= 48 && *i <= 57) {
            charOffset = *i - 48;
        } else if (*i >= 97 && *i <= 102) {
            charOffset = *i - 87;
        } else {
            return 0.0;
        }

        acc = (acc * 16) + charOffset;
    }

    return acc;
}

LDBoolean
LDi_bucketUser(
    const struct LDUser *const user,
    const char *const          segmentKey,
    const char *const          attribute,
    const char *const          salt,
    const int *const           seed,
    float *const               bucket)
{
    struct LDJSON *attributeValue;

    LD_ASSERT(user);
    LD_ASSERT(segmentKey);
    LD_ASSERT(attribute);
    LD_ASSERT(salt);
    LD_ASSERT(bucket);

    attributeValue = NULL;
    *bucket        = 0;

    if ((attributeValue = LDi_valueOfAttribute(user, attribute))) {
        char        raw[256], bucketableBuffer[256];
        const char *bucketable;
        int         snprintfStatus;

        bucketable = NULL;

        if (LDJSONGetType(attributeValue) == LDText) {
            bucketable = LDGetText(attributeValue);
        } else if (LDJSONGetType(attributeValue) == LDNumber) {
            if (snprintf(
                    bucketableBuffer,
                    sizeof(bucketableBuffer),
                    "%f",
                    LDGetNumber(attributeValue)) >= 0)
            {
                bucketable = bucketableBuffer;
            }
        }

        if (!bucketable) {
            LDJSONFree(attributeValue);

            return LDBooleanFalse;
        }

        if (seed) {
            if (user->secondary) {
                snprintfStatus = snprintf(
                    raw,
                    sizeof(raw),
                    "%d.%s.%s",
                    *seed,
                    bucketable,
                    user->secondary);
            } else {
                snprintfStatus =
                    snprintf(raw, sizeof(raw), "%d.%s", *seed, bucketable);
            }
        } else {
            if (user->secondary) {
                snprintfStatus = snprintf(
                    raw,
                    sizeof(raw),
                    "%s.%s.%s.%s",
                    segmentKey,
                    salt,
                    bucketable,
                    user->secondary);
            } else {
                snprintfStatus = snprintf(
                    raw, sizeof(raw), "%s.%s.%s", segmentKey, salt, bucketable);
            }
        }

        if (snprintfStatus >= 0 && (size_t)snprintfStatus < sizeof(raw)) {
            int         status;
            char        digest[21], encoded[17];
            const float longScale = 1152921504606846975.0;

            SHA1(digest, raw, strlen(raw));

            /* encodes to hex, and shortens, 16 characters in hex 8 bytes */
            status = hexify(
                (unsigned char *)digest,
                sizeof(digest) - 1,
                encoded,
                sizeof(encoded));
            LD_ASSERT(status == 16);

            encoded[15] = 0;

            *bucket = LDi_hexToDecimal(encoded) / longScale;

            LDJSONFree(attributeValue);

            return LDBooleanTrue;
        }

        LDJSONFree(attributeValue);
    }

    return LDBooleanFalse;
}

LDBoolean
LDi_variationIndexForUser(
    const struct LDJSON *const  varOrRoll,
    const struct LDUser *const  user,
    const char *const           key,
    const char *const           salt,
    LDBoolean *const            inExperiment,
    const struct LDJSON **const index)
{
    struct LDJSON *variation, *rollout, *variations, *weight, *subvariation,
        *rolloutKind;
    float     userBucket, sum;
    LDBoolean untrackedValue;

    LD_ASSERT(varOrRoll);
    LD_ASSERT(index);
    LD_ASSERT(inExperiment);

    variation     = NULL;
    rollout       = NULL;
    variations    = NULL;
    userBucket    = 0;
    sum           = 0;
    weight        = NULL;
    subvariation  = NULL;
    *inExperiment = LDBooleanFalse;

    variation = LDObjectLookup(varOrRoll, "variation");

    if (LDi_notNull(variation)) {
        if (LDJSONGetType(variation) != LDNumber) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return LDBooleanFalse;
        }

        *index = variation;

        return LDBooleanTrue;
    }

    LD_ASSERT(user);
    LD_ASSERT(salt);

    rollout = LDObjectLookup(varOrRoll, "rollout");

    if (!LDi_notNull(rollout)) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return LDBooleanFalse;
    }

    if (LDJSONGetType(rollout) != LDObject) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return LDBooleanFalse;
    }

    if (LDi_notNull((rolloutKind = LDObjectLookup(rollout, "kind")))) {
        if (LDJSONGetType(rolloutKind) != LDText) {
            LD_LOG(LD_LOG_ERROR, "rollout.kind expected string");

            return LDBooleanFalse;
        }

        if (strcmp(LDGetText(rolloutKind), "experiment") == 0) {
            *inExperiment = LDBooleanTrue;
        }
    }

    variations = LDObjectLookup(rollout, "variations");

    if (!LDi_notNull(variations)) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return LDBooleanFalse;
    }

    if (LDJSONGetType(variations) != LDArray) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return LDBooleanFalse;
    }

    if (LDCollectionGetSize(variations) == 0) {
        LD_LOG(LD_LOG_ERROR, "schema error");

        return LDBooleanFalse;
    }

    variation = LDGetIter(variations);
    LD_ASSERT(variation);

    {
        const char *         attribute;
        const struct LDJSON *seedJSON;
        int                  seedValue, *seedValueRef;

        seedValueRef = NULL;
        attribute    = LDi_getBucketAttribute(rollout);

        if (attribute == NULL) {
            LD_LOG(LD_LOG_ERROR, "failed to parse bucketBy");

            return LDBooleanFalse;
        }

        if (LDi_notNull((seedJSON = LDObjectLookup(rollout, "seed")))) {
            if (LDJSONGetType(seedJSON) != LDNumber) {
                LD_LOG(LD_LOG_ERROR, "rollout.seed expected number");

                return LDBooleanFalse;
            }

            seedValue    = (int)LDGetNumber(seedJSON);
            seedValueRef = &seedValue;
        }

        LDi_bucketUser(user, key, attribute, salt, seedValueRef, &userBucket);
    }

    for (; variation; variation = LDIterNext(variation)) {
        const struct LDJSON *untracked;

        weight = LDObjectLookup(variation, "weight");

        if (!LDi_notNull(weight)) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return LDBooleanFalse;
        }

        if (LDJSONGetType(weight) != LDNumber) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return LDBooleanFalse;
        }

        sum += LDGetNumber(weight) / 100000.0;

        subvariation = LDObjectLookup(variation, "variation");

        if (!LDi_notNull(subvariation)) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return LDBooleanFalse;
        }

        if (LDJSONGetType(subvariation) != LDNumber) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return LDBooleanFalse;
        }

        untrackedValue = LDBooleanFalse;

        if (LDi_notNull((untracked = LDObjectLookup(variation, "untracked")))) {
            if (LDJSONGetType(untracked) != LDBool) {
                LD_LOG(LD_LOG_ERROR, "untracked expected bool");

                return LDBooleanFalse;
            }

            untrackedValue = LDGetBool(untracked);
        }

        if (userBucket < sum) {
            *index = subvariation;

            if (*inExperiment && untrackedValue) {
                *inExperiment = LDBooleanFalse;
            }

            return LDBooleanTrue;
        }
    }

    /* The user's bucket value was greater than or equal to the end of the last
    bucket. This could happen due to a rounding error, or due to the fact that
    we are scaling to 100000 rather than 99999, or the flag data could contain
    buckets that don't actually add up to 100000. Rather than returning an error
    in this case (or changing the scaling, which would potentially change the
    results for *all* users), we will simply put the user in the last bucket.
    The loop ensures subvariation is the last element, and a size check above
    ensures there is at least one element. */

    *index = subvariation;

    if (*inExperiment && untrackedValue) {
        *inExperiment = LDBooleanFalse;
    }

    return LDBooleanTrue;
}

LDBoolean
LDi_getIndexForVariationOrRollout(
    const struct LDJSON *const  flag,
    const struct LDJSON *const  varOrRoll,
    const struct LDUser *const  user,
    LDBoolean *const            inExperiment,
    const struct LDJSON **const result)
{
    const struct LDJSON *jkey, *jsalt;
    const char *         key, *salt;

    LD_ASSERT(flag);
    LD_ASSERT(varOrRoll);
    LD_ASSERT(inExperiment);
    LD_ASSERT(result);

    jkey    = NULL;
    jsalt   = NULL;
    key     = NULL;
    salt    = NULL;
    *result = NULL;

    if (LDi_notNull(jkey = LDObjectLookup(flag, "key"))) {
        if (LDJSONGetType(jkey) != LDText) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return LDBooleanFalse;
        }

        key = LDGetText(jkey);
    }

    if (LDi_notNull(jsalt = LDObjectLookup(flag, "salt"))) {
        if (LDJSONGetType(jsalt) != LDText) {
            LD_LOG(LD_LOG_ERROR, "schema error");

            return LDBooleanFalse;
        }

        salt = LDGetText(jsalt);
    }

    if (!LDi_variationIndexForUser(
            varOrRoll, user, key, salt, inExperiment, result))
    {
        LD_LOG(LD_LOG_ERROR, "failed to get variation index");

        return LDBooleanFalse;
    }

    return LDBooleanTrue;
}
