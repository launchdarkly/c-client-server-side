#include <string.h>
#include <stdlib.h>

#include <launchdarkly/api.h>

#include "assertion.h"
#include "client.h"
#include "user.h"
#include "config.h"
#include "utility.h"

struct LDUser *
LDUserNew(const char *const key)
{
    struct LDUser *user;

    LD_ASSERT_API(key);

    #ifdef LAUNCHDARKLY_DEFENSIVE
        if (key == NULL) {
            LD_LOG(LD_LOG_WARNING, "LDUserNew NULL key");

            return NULL;
        }
    #endif

    if (!(user = (struct LDUser *)LDAlloc(sizeof(struct LDUser)))) {
        return NULL;
    }

    memset(user, 0, sizeof(struct LDUser));

    if (!LDSetString(&user->key, key)) {
        goto error;
    }

    if (!(user->privateAttributeNames = LDNewArray())) {
        goto error;
    }

    user->secondary = NULL;
    user->ip        = NULL;
    user->firstName = NULL;
    user->lastName  = NULL;
    user->email     = NULL;
    user->name      = NULL;
    user->avatar    = NULL;
    user->custom    = NULL;
    user->country   = NULL;

    return user;

  error:
    LDUserFree(user);

    return NULL;
}

void
LDUserFree(struct LDUser *const user)
{
    if (user) {
        LDFree(     user->key                   );
        LDFree(     user->secondary             );
        LDFree(     user->ip                    );
        LDFree(     user->firstName             );
        LDFree(     user->lastName              );
        LDFree(     user->email                 );
        LDFree(     user->name                  );
        LDFree(     user->avatar                );
        LDFree(     user->country               );
        LDJSONFree( user->custom                );
        LDJSONFree( user->privateAttributeNames );
        LDFree(     user                        );
    }
}

bool
LDUserSetAnonymous(struct LDUser *const user, const bool anon)
{
    LD_ASSERT_API(user);

    #ifdef LAUNCHDARKLY_DEFENSIVE
        if (user == NULL) {
            LD_LOG(LD_LOG_WARNING, "LDUserSetAnonymous NULL user");

            return false;
        }
    #endif

    user->anonymous = anon;

    return true;
}

bool
LDUserSetIP(struct LDUser *const user, const char *const ip)
{
    LD_ASSERT_API(user);

    #ifdef LAUNCHDARKLY_DEFENSIVE
        if (user == NULL) {
            LD_LOG(LD_LOG_WARNING, "LDUserSetIP NULL user");

            return false;
        }
    #endif

    return LDSetString(&user->ip, ip);
}

bool
LDUserSetFirstName(struct LDUser *const user, const char *const firstName)
{
    LD_ASSERT_API(user);

    return LDSetString(&user->firstName, firstName);
}

bool
LDUserSetLastName(struct LDUser *const user, const char *const lastName)
{
    LD_ASSERT_API(user);

    #ifdef LAUNCHDARKLY_DEFENSIVE
        if (user == NULL) {
            LD_LOG(LD_LOG_WARNING, "LDUserSetLastName NULL user");

            return false;
        }
    #endif

    return LDSetString(&user->lastName, lastName);
}

bool
LDUserSetEmail(struct LDUser *const user, const char *const email)
{
    LD_ASSERT_API(user);

    #ifdef LAUNCHDARKLY_DEFENSIVE
        if (user == NULL) {
            LD_LOG(LD_LOG_WARNING, "LDUserSetEmail NULL user");

            return false;
        }
    #endif

    return LDSetString(&user->email, email);
}

bool
LDUserSetName(struct LDUser *const user, const char *const name)
{
    LD_ASSERT_API(user);

    #ifdef LAUNCHDARKLY_DEFENSIVE
        if (user == NULL) {
            LD_LOG(LD_LOG_WARNING, "LDUserSetName NULL user");

            return false;
        }
    #endif

    return LDSetString(&user->name, name);
}

bool
LDUserSetAvatar(struct LDUser *const user, const char *const avatar)
{
    LD_ASSERT_API(user);

    #ifdef LAUNCHDARKLY_DEFENSIVE
        if (user == NULL) {
            LD_LOG(LD_LOG_WARNING, "LDUserSetAvatar NULL user");

            return false;
        }
    #endif

    return LDSetString(&user->avatar, avatar);
}

bool
LDUserSetCountry(struct LDUser *const user, const char *const country)
{
    LD_ASSERT_API(user);

    #ifdef LAUNCHDARKLY_DEFENSIVE
        if (user == NULL) {
            LD_LOG(LD_LOG_WARNING, "LDUserSetCountry NULL user");

            return false;
        }
    #endif

    return LDSetString(&user->country, country);
}

bool
LDUserSetSecondary(struct LDUser *const user, const char *const secondary)
{
    LD_ASSERT_API(user);

    #ifdef LAUNCHDARKLY_DEFENSIVE
        if (user == NULL) {
            LD_LOG(LD_LOG_WARNING, "LDUserSetSecondary NULL user");

            return false;
        }
    #endif

    return LDSetString(&user->secondary, secondary);
}

bool
LDUserSetCustom(struct LDUser *const user, struct LDJSON *const custom)
{
    LD_ASSERT_API(custom);

    #ifdef LAUNCHDARKLY_DEFENSIVE
        if (user == NULL) {
            LD_LOG(LD_LOG_WARNING, "LDUserSetCustom NULL user");

            return false;
        }
    #endif

    user->custom = custom;

    return true;
}

bool
LDUserAddPrivateAttribute(struct LDUser *const user,
    const char *const attribute)
{
    struct LDJSON *temp;

    LD_ASSERT_API(user);
    LD_ASSERT_API(attribute);

    #ifdef LAUNCHDARKLY_DEFENSIVE
        if (user == NULL) {
            LD_LOG(LD_LOG_WARNING, "LDUserAddPrivateAttribute NULL user");

            return false;
        }

        if (attribute == NULL) {
            LD_LOG(LD_LOG_WARNING, "LDUserAddPrivateAttribute NULL attribute");

            return false;
        }
    #endif

    if ((temp = LDNewText(attribute))) {
        return LDArrayPush(user->privateAttributeNames, temp);
    } else {
        return false;
    }
}

bool
LDi_textInArray(const struct LDJSON *const array, const char *const text)
{
    struct LDJSON *iter;

    LD_ASSERT(array);
    LD_ASSERT(text);

    for (iter = LDGetIter(array); iter; iter = LDIterNext(iter)) {
        if (strcmp(LDGetText(iter), text) == 0) {
            return true;
        }
    }

    return false;
}

static bool
isPrivateAttr(
    const struct LDConfig *const config,
    const struct LDUser *const   user,
    const char *const            key
) {
    bool global = false;

    if (config) {
        global = config->allAttributesPrivate ||
            LDi_textInArray(config->privateAttributeNames, key);
    }

    return global || LDi_textInArray(user->privateAttributeNames, key);
}

static bool
addHidden(struct LDJSON **const ref, const char *const value){
    struct LDJSON *text;

    LD_ASSERT(ref);
    LD_ASSERT(value);

    text = NULL;

    if (!(*ref)) {
        *ref = LDNewArray();

        if (!(*ref)) {
            return false;
        }
    }

    if (!(text = LDNewText(value))) {
        return false;
    }

    LDArrayPush(*ref, text);

    return true;
}

struct LDJSON *
LDUserToJSON(
    const struct LDConfig *const config,
    const struct LDUser *const   lduser,
    const bool                   redact
) {
    struct LDJSON *hidden, *json, *temp;

    LD_ASSERT(lduser);

    hidden = NULL;
    json   = NULL;
    temp   = NULL;

    if (!(json = LDNewObject())) {
        return NULL;
    }

    if (!(temp = LDNewText(lduser->key))) {
        LDJSONFree(json);

        return NULL;
    }

    if (!LDObjectSetKey(json, "key", temp)) {
        LDJSONFree(temp);
        LDJSONFree(json);

        return NULL;
    }

    if (lduser->anonymous) {
        if (!(temp = LDNewBool(lduser->anonymous))) {
            LDJSONFree(json);

            return NULL;
        }

        if (!LDObjectSetKey(json, "anonymous", temp)) {
            LDJSONFree(temp);
            LDJSONFree(json);

            return NULL;
        }
    }

    #define addstring(field)                                                   \
        if (lduser->field) {                                                   \
            if (redact && isPrivateAttr(config, lduser, #field)) {             \
                if (!addHidden(&hidden, #field)) {                             \
                    LDJSONFree(json);                                          \
                                                                               \
                    return NULL;                                               \
                }                                                              \
            }                                                                  \
            else {                                                             \
                if (!(temp = LDNewText(lduser->field))) {                      \
                    LDJSONFree(json);                                          \
                                                                               \
                    return NULL;                                               \
                }                                                              \
                                                                               \
                if (!LDObjectSetKey(json, #field, temp)) {                     \
                    LDJSONFree(json);                                          \
                                                                               \
                    return NULL;                                               \
                }                                                              \
            }                                                                  \
        }                                                                      \

    addstring(secondary);
    addstring(ip);
    addstring(firstName);
    addstring(lastName);
    addstring(email);
    addstring(name);
    addstring(avatar);
    addstring(country);

    if (lduser->custom) {
        struct LDJSON *const custom = LDJSONDuplicate(lduser->custom);

        if (!custom) {
            LDJSONFree(json);

            return NULL;
        }

        if (redact && LDJSONGetType(custom) == LDObject) {
            struct LDJSON *item = LDGetIter(custom);

            while(item) {
                /* must record next to make delete safe */
                struct LDJSON *const next = LDIterNext(item);

                if (isPrivateAttr(config, lduser, LDIterKey(item))) {
                    if (!addHidden(&hidden, LDIterKey(item))) {
                        LDJSONFree(json);
                        LDJSONFree(custom);

                        return NULL;
                    }

                    LDObjectDeleteKey(custom, LDIterKey(item));
                }

                item = next;
            }
        }

        if (!LDObjectSetKey(json, "custom", custom)) {
            LDJSONFree(custom);
            LDJSONFree(json);

            return NULL;
        }
    }

    if (hidden) {
        if (!LDObjectSetKey(json, "privateAttrs", hidden)) {
            LDJSONFree(json);

            return NULL;
        }
    }

    return json;

    #undef addstring
}

struct LDJSON *
LDi_valueOfAttribute(const struct LDUser *const user,
    const char *const attribute)
{
    LD_ASSERT(user);
    LD_ASSERT(attribute);

    if (strcmp(attribute, "key") == 0) {
        if (user->key) {
            return LDNewText(user->key);
        }
    } else if (strcmp(attribute, "ip") == 0) {
        if (user->ip) {
            return LDNewText(user->ip);
        }
    } else if (strcmp(attribute, "email") == 0) {
        if (user->email) {
            return LDNewText(user->email);
        }
    } else if (strcmp(attribute, "firstName") == 0) {
        if (user->firstName) {
            return LDNewText(user->firstName);
        }
    } else if (strcmp(attribute, "lastName") == 0) {
        if (user->lastName) {
            return LDNewText(user->lastName);
        }
    } else if (strcmp(attribute, "avatar") == 0) {
        if (user->avatar) {
            return LDNewText(user->avatar);
        }
    } else if (strcmp(attribute, "country") == 0) {
        if (user->country) {
            return LDNewText(user->country);
        }
    } else if (strcmp(attribute, "name") == 0) {
        if (user->name) {
            return LDNewText(user->name);
        }
    } else if (strcmp(attribute, "anonymous") == 0) {
        return LDNewBool(user->anonymous);
    } else if (user->custom) {
        const struct LDJSON *node = NULL;

        LD_ASSERT(LDJSONGetType(user->custom) == LDObject);

        if ((node = LDObjectLookup(user->custom, attribute))) {
            return LDJSONDuplicate(node);
        }

        return NULL;
    }

    return NULL;
}
