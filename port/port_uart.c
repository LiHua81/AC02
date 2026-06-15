#include "port_uart.h"
#include "stm32f1xx_hal.h"
#include <string.h>

extern UART_HandleTypeDef huart2;

#define UART_RX_DMA_SIZE 128
#define UART_TX_BUF_SIZE 128

/* DMA RX circular buffer — written by hardware */
static uint8_t rx_dma_buf[UART_RX_DMA_SIZE];
static volatile int rx_dma_last_pos = 0;

/* Line assembly buffer — written by uart_process_byte */
static char     rx_buffer[UART_RX_DMA_SIZE];
static volatile int rx_head = 0;
static volatile int rx_tail = 0;
static volatile int rx_line_ready = 0;

/* TX */
static char     tx_buffer[UART_TX_BUF_SIZE];
static volatile int tx_busy = 0;

static void uart_process_byte(uint8_t c) {
    if (c == '\r') return;
    if (c == '\n') {
        rx_buffer[rx_head] = '\0';
        rx_line_ready = 1;
        return;
    }
    int next = (rx_head + 1) % UART_RX_DMA_SIZE;
    if (next != rx_tail) {
        rx_buffer[rx_head] = (char)c;
        rx_head = next;
    }
}

void uart_init(void) {
    HAL_NVIC_SetPriority(USART2_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    HAL_UART_Receive_DMA(&huart2, rx_dma_buf, UART_RX_DMA_SIZE);
    __HAL_UART_ENABLE_IT(&huart2, UART_IT_IDLE);
}

void uart_send(const char *str) {
    if (tx_busy) return;

    size_t len = strlen(str);
    if (len == 0 || len > UART_TX_BUF_SIZE) return;

    /* 例外：port 层通常禁止 __disable_irq。
     * 但 tx_busy 必须原子保护：DMA TC 回调（优先级 0）会抢占并清零 tx_busy，
     * 如果 memcpy 和 tx_busy=1 之间被抢占，会导致 DMA 写入正在拷贝的缓冲区。 */
    __disable_irq();
    if (tx_busy) {
        __enable_irq();
        return;
    }
    tx_busy = 1;
    memcpy(tx_buffer, str, len);
    __enable_irq();

    HAL_UART_Transmit_DMA(&huart2, (uint8_t *)tx_buffer, (uint16_t)len);
}

int uart_read_line(char *buf) {
    if (!rx_line_ready) return 0;

    int i = 0;
    while (rx_tail != rx_head) {
        buf[i++] = rx_buffer[rx_tail];
        rx_tail = (rx_tail + 1) % UART_RX_DMA_SIZE;
    }
    buf[i] = '\0';
    rx_tail = 0;
    rx_head = 0;
    rx_line_ready = 0;
    return 1;
}

void uart_on_idle(void) {
    int current = UART_RX_DMA_SIZE - __HAL_DMA_GET_COUNTER(huart2.hdmarx);
    int count;

    if (current >= rx_dma_last_pos) {
        count = current - rx_dma_last_pos;
    } else {
        /* wrap-around: process tail + head */
        int tail_len = UART_RX_DMA_SIZE - rx_dma_last_pos;
        for (int i = 0; i < tail_len; i++)
            uart_process_byte(rx_dma_buf[rx_dma_last_pos + i]);
        count = current;
    }

    if (count > 0) {
        int start = current - count;
        for (int i = 0; i < count; i++)
            uart_process_byte(rx_dma_buf[start + i]);
    }

    rx_dma_last_pos = current;
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART2) {
        tx_busy = 0;
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART2) {
        tx_busy = 0;
        HAL_UART_Receive_DMA(&huart2, rx_dma_buf, UART_RX_DMA_SIZE);
        rx_dma_last_pos = 0;
    }
}
