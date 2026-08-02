#ifndef SHOT_PREDICTION_H
#define SHOT_PREDICTION_H

#include <math.h>

// Slower trends cannot produce a useful ETA inside the 50 s shot window and
// are easily manufactured by float cancellation when a scale repeats a value.
#define MIN_WEIGHT_TREND_G_PER_S 0.01f

// Fit weight = slope * time + intercept using centered sums, then solve for
// targetWeight. expectedEndS is always set to the finite maxDurationS fallback.
// The return value says whether a reliable forward trend was available.
inline bool predictShotEnd(const float* timeS, const float* weight, int count,
                           float targetWeight, float currentTimeS,
                           float maxDurationS, float* expectedEndS) {
  if (!expectedEndS) {
    return false;
  }
  *expectedEndS = maxDurationS;

  if (!timeS || !weight || count < 2 ||
      !isfinite(targetWeight) || !isfinite(currentTimeS) ||
      !isfinite(maxDurationS)) {
    return false;
  }

  double meanX = 0.0;
  double meanY = 0.0;
  for (int i = 0; i < count; i++) {
    if (!isfinite(timeS[i]) || !isfinite(weight[i])) {
      return false;
    }
    meanX += timeS[i];
    meanY += weight[i];
  }
  meanX /= count;
  meanY /= count;

  double sumXX = 0.0;
  double sumXY = 0.0;
  for (int i = 0; i < count; i++) {
    double dx = timeS[i] - meanX;
    double dy = weight[i] - meanY;
    sumXX += dx * dx;
    sumXY += dx * dy;
  }

  if (!isfinite(sumXX) || !isfinite(sumXY) || sumXX <= 1e-9) {
    return false;
  }

  double slope = sumXY / sumXX;
  if (!isfinite(slope) || slope < MIN_WEIGHT_TREND_G_PER_S) {
    return false;
  }

  double predictedEndS = meanX + (targetWeight - meanY) / slope;
  if (!isfinite(predictedEndS)) {
    return false;
  }

  if (predictedEndS < currentTimeS) {
    predictedEndS = currentTimeS;
  }
  if (predictedEndS > maxDurationS) {
    predictedEndS = maxDurationS;
  }

  *expectedEndS = static_cast<float>(predictedEndS);
  return true;
}

#endif // SHOT_PREDICTION_H
