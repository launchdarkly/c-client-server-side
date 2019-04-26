
/*!
 * @file ldstreaming.h
 * @brief Internal API Interface for Networking. Header primarily for testing.
 */


#pragma once

#include <curl/curl.h>

#include "network.h"

struct StreamContext {
    char *memory;
    size_t size;
    bool active;
    struct curl_slist *headers;
    char eventName[256];
    char *dataBuffer;
    struct LDClient *client;
};

bool LDi_parsePath(const char *path, enum FeatureKind *const kind,
    const char **const key);

bool LDi_onSSE(struct StreamContext *const context, const char *line);
