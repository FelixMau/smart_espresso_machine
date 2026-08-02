#include <Arduino.h>
#include <WiFi.h>  // Keep the firmware's web dependencies complete in test builds.
#include <unity.h>

#include "wifi_credentials.h"

void testSameSsidDifferentPasswordNeedsFallback() {
  TEST_ASSERT_TRUE(wifiCredentialPairsDiffer("home", "typo", "home", "correct"));
}

void testDifferentSsidNeedsFallback() {
  TEST_ASSERT_TRUE(wifiCredentialPairsDiffer("guest", "secret", "home", "secret"));
}

void testIdenticalPairDoesNotNeedFallback() {
  TEST_ASSERT_FALSE(wifiCredentialPairsDiffer("home", "correct", "home", "correct"));
}

void setup() {
  UNITY_BEGIN();
  RUN_TEST(testSameSsidDifferentPasswordNeedsFallback);
  RUN_TEST(testDifferentSsidNeedsFallback);
  RUN_TEST(testIdenticalPairDoesNotNeedFallback);
  UNITY_END();
}

void loop() {}
