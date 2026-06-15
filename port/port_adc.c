#include "port_adc.h"
#include "stm32f1xx_hal.h"
#include <string.h>

extern ADC_HandleTypeDef hadc1;

#define ADC_BATCH_SIZE  50
#define ADC_MAX_VALUE   4095.0f
#define ADC_REF_VOLTAGE 3.3f
#define ADC_VSCALE      17.14f   /* ±1.65V ADC → ±28.3V peak (20Vrms × √2) */
#define CALIB_SAMPLES   200

static uint16_t adc_dma_buf[ADC_BATCH_SIZE];
static float    adc_snapshot[ADC_BATCH_SIZE];
static volatile int adc_snapshot_ready = 0;
static float adc_dc_bias = 0.0f;

static inline float raw_to_volt(uint16_t raw) {
    return (((float)raw) * (ADC_REF_VOLTAGE / ADC_MAX_VALUE) - adc_dc_bias) * ADC_VSCALE;
}

void adc_calibrate(void) {
    uint32_t sum = 0;
    uint16_t sample;

    HAL_ADC_Start(&hadc1);
    for (int i = 0; i < CALIB_SAMPLES; i++) {
        HAL_ADC_PollForConversion(&hadc1, 10);
        sample = (uint16_t)HAL_ADC_GetValue(&hadc1);
        sum += sample;
    }
    HAL_ADC_Stop(&hadc1);

    adc_dc_bias = ((float)sum / CALIB_SAMPLES) * (ADC_REF_VOLTAGE / ADC_MAX_VALUE);
}

void adc_start_dma(void) {
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_dma_buf, ADC_BATCH_SIZE);
}

float adc_read_v(void) {
    return adc_snapshot[ADC_BATCH_SIZE - 1];
}

void adc_get_batch(float *buf, int count) {
    int n = (count < ADC_BATCH_SIZE) ? count : ADC_BATCH_SIZE;

    if (adc_snapshot_ready) {
        memcpy(buf, adc_snapshot, (size_t)n * sizeof(float));
        adc_snapshot_ready = 0;
    } else {
        for (int i = 0; i < n; i++) {
            buf[i] = raw_to_volt(adc_dma_buf[i]);
        }
    }

    for (int i = n; i < count; i++) {
        buf[i] = 0.0f;
    }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc) {
    if (hadc->Instance == ADC1) {
        for (int i = 0; i < ADC_BATCH_SIZE; i++) {
            adc_snapshot[i] = raw_to_volt(adc_dma_buf[i]);
        }
        adc_snapshot_ready = 1;
    }
}
