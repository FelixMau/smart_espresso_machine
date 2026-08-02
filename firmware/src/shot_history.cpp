#include "shot_history.h"

#include <time.h>

#include "debug.h"

ShotRecord shotHistory[HISTORY_MAX_SHOTS];
int shotHistoryCount = 0;
int shotHistoryWriteIdx = 0;
SemaphoreHandle_t shotHistoryLock = nullptr;

static uint32_t nextShotId = 1;

void initShotHistory() {
  shotHistoryLock = xSemaphoreCreateMutex();
}

void recordShot(const float* timeS, const float* weight, const float* pressure,
                int datapoints, float durationS, float peakPressure, int endReason) {
  if (datapoints <= 0) {
    return;
  }

  if (shotHistoryLock && xSemaphoreTake(shotHistoryLock, pdMS_TO_TICKS(100)) != pdTRUE) {
    DEBUG_SHOT_PRINT("Shot history lock busy - shot not recorded");
    return;
  }

  ShotRecord& rec = shotHistory[shotHistoryWriteIdx];
  rec.id = nextShotId++;
  rec.timestamp = (uint32_t)time(nullptr);
  rec.durationS = durationS;
  rec.finalWeight = weight[datapoints - 1];
  rec.peakPressure = peakPressure;
  rec.endReason = endReason;

  // Downsample by stride, always keeping the last point
  int stride = (datapoints + HISTORY_MAX_POINTS - 1) / HISTORY_MAX_POINTS;
  int numPoints = 0;
  for (int i = 0; i < datapoints && numPoints < HISTORY_MAX_POINTS; i += stride) {
    rec.timeS[numPoints] = timeS[i];
    rec.weight[numPoints] = weight[i];
    rec.pressure[numPoints] = pressure[i];
    numPoints++;
  }
  if (rec.timeS[numPoints - 1] != timeS[datapoints - 1]) {
    if (numPoints < HISTORY_MAX_POINTS) {
      numPoints++;
    }
    rec.timeS[numPoints - 1] = timeS[datapoints - 1];
    rec.weight[numPoints - 1] = weight[datapoints - 1];
    rec.pressure[numPoints - 1] = pressure[datapoints - 1];
  }
  rec.numPoints = numPoints;

  shotHistoryWriteIdx = (shotHistoryWriteIdx + 1) % HISTORY_MAX_SHOTS;
  if (shotHistoryCount < HISTORY_MAX_SHOTS) {
    shotHistoryCount++;
  }

  if (shotHistoryLock) {
    xSemaphoreGive(shotHistoryLock);
  }

  DEBUG_SHOT_PRINT("Shot #%lu recorded to history (%d points, %.1f g, %.1f s, peak %.1f bar)",
                   (unsigned long)rec.id, rec.numPoints, rec.finalWeight,
                   rec.durationS, rec.peakPressure);
}
