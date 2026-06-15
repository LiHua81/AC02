#ifndef PORT_ADC_H
#define PORT_ADC_H

void adc_calibrate(void);
void adc_start_dma(void);
float adc_read_v(void);
void adc_get_batch(float *buf, int count);

#endif
