#include <Arduino.h>
#include <unity.h>

#include "pressure_profile.h"

static bool parse(const char* times, const char* pressures,
                  PressureProfile* profile = nullptr) {
  PressureProfile local = {};
  String error;
  return parsePressureProfile(times, pressures, profile ? profile : &local, &error);
}

void test_valid_profile_parses() {
  PressureProfile profile = {};
  TEST_ASSERT_TRUE(parse("0,5,20,-10,-5", "2,9,6,5,4", &profile));
  TEST_ASSERT_EQUAL(3, profile.numByTime);
  TEST_ASSERT_EQUAL(2, profile.numByTimeLeft);
  TEST_ASSERT_EQUAL_FLOAT(10.0f, profile.byTimeLeft[0].timeLeftS);
  TEST_ASSERT_EQUAL_FLOAT(5.0f, profile.byTimeLeft[1].timeLeftS);
}

void test_nonfinite_and_partial_numbers_are_rejected() {
  TEST_ASSERT_FALSE(parse("0", "nan"));
  TEST_ASSERT_FALSE(parse("0", "inf"));
  TEST_ASSERT_FALSE(parse("0", "1e999"));
  TEST_ASSERT_FALSE(parse("0", "9O"));
  TEST_ASSERT_FALSE(parse("abc", "9"));
}

void test_empty_and_mismatched_lists_are_rejected() {
  TEST_ASSERT_FALSE(parse("", ""));
  TEST_ASSERT_FALSE(parse("0,", "2,"));
  TEST_ASSERT_FALSE(parse("0,,5", "2,,9"));
  TEST_ASSERT_FALSE(parse("0,5", "2"));
  TEST_ASSERT_FALSE(parse("0", "2,9"));
}

void test_ranges_are_enforced() {
  TEST_ASSERT_TRUE(parse("0,50,-50", "0,16,16"));
  TEST_ASSERT_FALSE(parse("0,50.1", "2,9"));
  TEST_ASSERT_FALSE(parse("0,-50.1", "2,9"));
  TEST_ASSERT_FALSE(parse("0", "-0.1"));
  TEST_ASSERT_FALSE(parse("0", "16.1"));
}

void test_profile_must_start_at_zero_and_be_ordered() {
  TEST_ASSERT_FALSE(parse("1,5", "2,9"));
  TEST_ASSERT_FALSE(parse("0,10,5", "2,9,6"));
  TEST_ASSERT_FALSE(parse("0,-5,-10", "2,4,6"));
  TEST_ASSERT_FALSE(parse("0,5,5", "2,9,6"));
  TEST_ASSERT_FALSE(parse("0,-5,-5", "2,4,3"));
}

void test_category_capacity_is_enforced() {
  TEST_ASSERT_TRUE(parse("0,1,2,3,4,5,6,7,-8,-7,-6,-5,-4,-3,-2,-1",
                         "2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2"));
  TEST_ASSERT_FALSE(parse("0,1,2,3,4,5,6,7,8", "2,2,2,2,2,2,2,2,2"));
}

void setup() {
  UNITY_BEGIN();
  RUN_TEST(test_valid_profile_parses);
  RUN_TEST(test_nonfinite_and_partial_numbers_are_rejected);
  RUN_TEST(test_empty_and_mismatched_lists_are_rejected);
  RUN_TEST(test_ranges_are_enforced);
  RUN_TEST(test_profile_must_start_at_zero_and_be_ordered);
  RUN_TEST(test_category_capacity_is_enforced);
  UNITY_END();
}

void loop() {}
