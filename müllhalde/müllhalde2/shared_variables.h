#ifndef SHARED_VARIABLES_H
#define SHARED_VARIABLES_H

#include <Arduino.h>

// Define ENDTYPE enum
typedef enum {BUTTON, WEIGHT, TIME, UNDEF} ENDTYPE;

// Define Shot struct
struct Shot {
  float start_timestamp_s;
  float shotTimer;
  float end_s;
  float expected_end_s;
  float weight[1000];
  float time_s[1000];
  int datapoints;
  bool brewing;
  ENDTYPE end;
};

// Declare shared variables
extern float goalWeight;
extern float weightOffset;
extern float currentWeight;
extern Shot shot;

#endif // SHARED_VARIABLES_H
