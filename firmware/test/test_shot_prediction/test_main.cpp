#include <Arduino.h>
#include <unity.h>

#include "shot_prediction.h"

static void fillLinear(float* times, float* weights, float slope, float intercept) {
  for (int i = 0; i < 10; i++) {
    times[i] = 5.0f + i * 0.25f;
    weights[i] = slope * times[i] + intercept;
  }
}

void test_normal_rising_trend_predicts_target_time() {
  float times[10], weights[10], expected = NAN;
  fillLinear(times, weights, 2.0f, 5.0f);

  TEST_ASSERT_TRUE(predictShotEnd(times, weights, 10, 35.0f, 7.25f, 50.0f, &expected));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 15.0f, expected);
}

void test_flat_trend_uses_finite_fallback() {
  float times[10], weights[10], expected = NAN;
  fillLinear(times, weights, 0.0f, 15.0f);

  TEST_ASSERT_FALSE(predictShotEnd(times, weights, 10, 35.0f, 7.25f, 50.0f, &expected));
  TEST_ASSERT_TRUE(isfinite(expected));
  TEST_ASSERT_EQUAL_FLOAT(50.0f, expected);
}

void test_flat_trend_at_target_does_not_produce_nan() {
  float times[10], weights[10], expected = NAN;
  fillLinear(times, weights, 0.0f, 35.0f);

  TEST_ASSERT_FALSE(predictShotEnd(times, weights, 10, 35.0f, 7.25f, 50.0f, &expected));
  TEST_ASSERT_EQUAL_FLOAT(50.0f, expected);
}

void test_duplicate_timestamps_use_fallback() {
  float times[10], weights[10], expected = NAN;
  for (int i = 0; i < 10; i++) {
    times[i] = 5.0f;
    weights[i] = 10.0f + i;
  }

  TEST_ASSERT_FALSE(predictShotEnd(times, weights, 10, 35.0f, 5.0f, 50.0f, &expected));
  TEST_ASSERT_EQUAL_FLOAT(50.0f, expected);
}

void test_negative_and_near_zero_trends_use_fallback() {
  float times[10], weights[10], expected = NAN;
  fillLinear(times, weights, -1.0f, 30.0f);
  TEST_ASSERT_FALSE(predictShotEnd(times, weights, 10, 35.0f, 7.25f, 50.0f, &expected));
  TEST_ASSERT_EQUAL_FLOAT(50.0f, expected);

  fillLinear(times, weights, 0.001f, 15.0f);
  TEST_ASSERT_FALSE(predictShotEnd(times, weights, 10, 35.0f, 7.25f, 50.0f, &expected));
  TEST_ASSERT_EQUAL_FLOAT(50.0f, expected);
}

void test_nonfinite_input_uses_fallback() {
  float times[10], weights[10], expected = NAN;
  fillLinear(times, weights, 2.0f, 5.0f);
  weights[4] = INFINITY;

  TEST_ASSERT_FALSE(predictShotEnd(times, weights, 10, 35.0f, 7.25f, 50.0f, &expected));
  TEST_ASSERT_TRUE(isfinite(expected));
  TEST_ASSERT_EQUAL_FLOAT(50.0f, expected);
}

void setup() {
  UNITY_BEGIN();
  RUN_TEST(test_normal_rising_trend_predicts_target_time);
  RUN_TEST(test_flat_trend_uses_finite_fallback);
  RUN_TEST(test_flat_trend_at_target_does_not_produce_nan);
  RUN_TEST(test_duplicate_timestamps_use_fallback);
  RUN_TEST(test_negative_and_near_zero_trends_use_fallback);
  RUN_TEST(test_nonfinite_input_uses_fallback);
  UNITY_END();
}

void loop() {}
