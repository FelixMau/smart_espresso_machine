#ifndef SHOT_HISTORY_H
#define SHOT_HISTORY_H

#include <Arduino.h>
#include "debug.h"

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
  float duration_s;                   // Shot duration
  float finalWeight;                  // Weight at shot stop (before drip)
  float peakPressure;                 // Highest pressure seen during the shot
  int endReason;                      // ENDTYPE cast to int (BUTTON/WEIGHT/TIME/UNDEF)
  int numPoints;                      // Valid points in the arrays below
  float time_s[HISTORY_MAX_POINTS];   // Downsampled time axis
  float weight[HISTORY_MAX_POINTS];   // Downsampled weight trajectory
};

ShotRecord shotHistory[HISTORY_MAX_SHOTS];
int shotHistoryCount = 0;     // How many slots are filled (max HISTORY_MAX_SHOTS)
int shotHistoryWriteIdx = 0;  // Next slot to overwrite (ring)
uint32_t nextShotId = 1;

// Guards shotHistory against concurrent access: the control task writes at
// shot end while the async web server task may be serializing a read.
SemaphoreHandle_t shotHistoryLock = nullptr;

void initShotHistory() {
  shotHistoryLock = xSemaphoreCreateMutex();
}

// Snapshot + downsample a finished shot's trajectory into the ring buffer.
// endReason is the ENDTYPE value at shot end (before it gets reset).
void recordShot(const float* time_s, const float* weight, int datapoints,
                float duration_s, float peakPressure, int endReason) {
  if (datapoints <= 0) {
    return;
  }

  if (shotHistoryLock && xSemaphoreTake(shotHistoryLock, pdMS_TO_TICKS(100)) != pdTRUE) {
    DEBUG_SHOT_PRINT("Shot history lock busy - shot not recorded");
    return;
  }

  ShotRecord& rec = shotHistory[shotHistoryWriteIdx];
  rec.id = nextShotId++;
  rec.duration_s = duration_s;
  rec.finalWeight = weight[datapoints - 1];
  rec.peakPressure = peakPressure;
  rec.endReason = endReason;

  // Downsample by stride, always keeping the last point
  int stride = (datapoints + HISTORY_MAX_POINTS - 1) / HISTORY_MAX_POINTS;
  int m = 0;
  for (int i = 0; i < datapoints && m < HISTORY_MAX_POINTS; i += stride) {
    rec.time_s[m] = time_s[i];
    rec.weight[m] = weight[i];
    m++;
  }
  if (rec.time_s[m - 1] != time_s[datapoints - 1]) {
    if (m < HISTORY_MAX_POINTS) {
      m++;
    }
    rec.time_s[m - 1] = time_s[datapoints - 1];
    rec.weight[m - 1] = weight[datapoints - 1];
  }
  rec.numPoints = m;

  shotHistoryWriteIdx = (shotHistoryWriteIdx + 1) % HISTORY_MAX_SHOTS;
  if (shotHistoryCount < HISTORY_MAX_SHOTS) {
    shotHistoryCount++;
  }

  if (shotHistoryLock) {
    xSemaphoreGive(shotHistoryLock);
  }

  DEBUG_SHOT_PRINT("Shot #%lu recorded to history (%d points, %.1f g, %.1f s, peak %.1f bar)",
                   (unsigned long)rec.id, rec.numPoints, rec.finalWeight,
                   rec.duration_s, rec.peakPressure);
}

#endif // SHOT_HISTORY_H
