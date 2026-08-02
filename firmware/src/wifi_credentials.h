#ifndef WIFI_CREDENTIALS_H
#define WIFI_CREDENTIALS_H

#include <string.h>

// A fallback is redundant only when both the network name and secret match.
inline bool wifiCredentialPairsDiffer(const char* firstSsid, const char* firstPass,
                                      const char* secondSsid, const char* secondPass) {
  return strcmp(firstSsid, secondSsid) != 0 || strcmp(firstPass, secondPass) != 0;
}

#endif  // WIFI_CREDENTIALS_H
