/*!
 * @file variations.h
 * @brief Public API Interface for evaluation variations
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <launchdarkly/user.h>
#include <launchdarkly/json.h>
#include <launchdarkly/export.h>

enum LDEvalReason {
    LD_UNKNOWN = 0,
    LD_ERROR,
    LD_OFF,
    LD_PREREQUISITE_FAILED,
    LD_TARGET_MATCH,
    LD_RULE_MATCH,
    LD_FALLTHROUGH
};

enum LDEvalErrorKind {
    LD_CLIENT_NOT_READY,
    LD_NULL_KEY,
    LD_STORE_ERROR,
    LD_FLAG_NOT_FOUND,
    LD_USER_NOT_SPECIFIED,
    LD_MALFORMED_FLAG,
    LD_WRONG_TYPE
};

struct LDDetailsRule {
    unsigned int ruleIndex;
    char *id;
};

struct LDDetails {
    unsigned int variationIndex;
    bool hasVariation;
    enum LDEvalReason reason;
    union {
        enum LDEvalErrorKind errorKind;
        char *prerequisiteKey;
        struct LDDetailsRule rule;
    } extra;
};

LD_EXPORT(void) LDDetailsInit(struct LDDetails *const details);

LD_EXPORT(void) LDDetailsClear(struct LDDetails *const details);

LD_EXPORT(const char *) LDEvalReasonKindToString(const enum LDEvalReason kind);

LD_EXPORT(const char *) LDEvalErrorKindToString(
    const enum LDEvalErrorKind kind);

LD_EXPORT(struct LDJSON *) LDReasonToJSON(
    const struct LDDetails *const details);

LD_EXPORT(bool) LDBoolVariation(struct LDClient *const client,
    struct LDUser *const user, const char *const key, const bool fallback,
    struct LDDetails *const details);

LD_EXPORT(int) LDIntVariation(struct LDClient *const client,
    struct LDUser *const user, const char *const key, const int fallback,
    struct LDDetails *const details);

LD_EXPORT(double) LDDoubleVariation(struct LDClient *const client,
    struct LDUser *const user, const char *const key, const double fallback,
    struct LDDetails *const details);

LD_EXPORT(char *) LDStringVariation(struct LDClient *const client,
    struct LDUser *const user, const char *const key,
    const char* const fallback, struct LDDetails *const details);

LD_EXPORT(struct LDJSON *) LDJSONVariation(struct LDClient *const client,
    struct LDUser *const user, const char *const key,
    const struct LDJSON *const fallback, struct LDDetails *const details);

LD_EXPORT(struct LDJSON *) LDAllFlags(struct LDClient *const client,
    struct LDUser *const user);
