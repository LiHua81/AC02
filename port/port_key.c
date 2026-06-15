#include "port_key.h"
#include "stm32f1xx_hal.h"

#define KEY_DEBOUNCE_COUNT 2

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    int count;
    key_event_t event;
} key_state_t;

static key_state_t keys[] = {
    {GPIOC, GPIO_PIN_5, 0, KEY0_SHORT},
    {GPIOA, GPIO_PIN_15, 0, KEY1_SHORT}
};

key_event_t key_scan(void) {
    int i;
    
    for (i = 0; i < 2; i++) {
        if (HAL_GPIO_ReadPin(keys[i].port, keys[i].pin) == GPIO_PIN_RESET) {
            keys[i].count++;
            if (keys[i].count >= KEY_DEBOUNCE_COUNT) {
                keys[i].count = 0;
                return keys[i].event;
            }
        } else {
            keys[i].count = 0;
        }
    }
    
    return KEY_NONE;
}