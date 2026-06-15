#include "algo_park.h"

float park_vd(float vAlpha, float vBeta, float cosT, float sinT) {
    return vAlpha * cosT + vBeta * sinT;
}

float park_vq(float vAlpha, float vBeta, float cosT, float sinT) {
    return -vAlpha * sinT + vBeta * cosT;
}