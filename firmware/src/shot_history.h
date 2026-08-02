#ifndef SHOT_HISTORY_H
#define SHOT_HISTORY_H

#include <Arduino.h>

// ============================================================================
// SHOT HISTORY (RAM-only ring buffer)
// ============================================================================
// Keeps the last few shots with a downsampled weight trajectory so the web
// dashboard can plot and compare them. Lost on reboot by design.
//
// A full trajectory is up to 1000 points (~8 KB); downsampled to
// HISTORY_MAX_POINTS it is ~800 B per shot, so 5 shots fit comfortably in RAM.

#define HISTORY_MAX_SHOTS 5
#define HISTORY_MAX_POINTS 100

struct ShotRecord {
  uint32_t id;                        // Monotonically increasing shot number
  uint32_t timestamp;                 // Unix epoch at shot end (0/near-1970 if NTP never synced)
  float durationS;                    // Shot duration
  float finalWeight;                  // Weight at shot stop (before drip)
  float peakPressure;                 // Highest pressure seen during the shot
  int endReason;                      // EndType cast to int (BUTTON/WEIGHT/TIME/UNDEF)
  int numPoints;                      // Valid points in the arrays below
  float timeS[HISTORY_MAX_POINTS];    // Downsampled time axis
  float weight[HISTORY_MAX_POINTS];   // Downsampled weight trajectory
  float pressure[HISTORY_MAX_POINTS]; // Downsampled pressure trajectory
};

extern ShotRecord shotHistory[HISTORY_MAX_SHOTS];
extern int shotHistoryCount;     // How many slots are filled (max HISTORY_MAX_SHOTS)
extern int shotHistoryWriteIdx;  // Next slot to overwrite (ring)

// Guards shotHistory against concurrent access: the control task writes at
// shot end while the async web server task may be serializing a read.
extern SemaphoreHandle_t shotHistoryLock;

void initShotHistory();

// Snapshot + downsample a finished shot's trajectory into the ring buffer.
// endReason is the EndType value at shot end (before it gets reset).
void recordShot(const float* timeS, const float* weight, const float* pressure,
                int datapoints, float durationS, float peakPressure, int endReason);

#endif // SHOT_HISTORY_H
