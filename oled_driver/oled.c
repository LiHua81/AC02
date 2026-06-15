#include "oled.h"
#include "font_8x16.h"
#include "font_6x10.h"
#include "font_4x6.h"
#include <string.h>

#define SSD1306_CMD_DISPLAY_OFF        0xAE
#define SSD1306_CMD_DISPLAY_ON         0xAF
#define SSD1306_CMD_SET_MEM_ADDR_MODE  0x20
#define SSD1306_CMD_SET_COL_ADDR       0x21
#define SSD1306_CMD_SET_PAGE_ADDR      0x22
#define SSD1306_CMD_SET_DISP_START_LINE 0x40
#define SSD1306_CMD_SET_SEGMENT_REMAP  0xA0
#define SSD1306_CMD_SET_MUX_RATIO      0xA8
#define SSD1306_CMD_SET_COM_SCAN_DIR   0xC0
#define SSD1306_CMD_SET_COM_PIN_CFG    0xDA
#define SSD1306_CMD_SET_CONTRAST       0x81
#define SSD1306_CMD_SET_PRECHARGE      0xD9
#define SSD1306_CMD_SET_VCOM_DESEL     0xDB
#define SSD1306_CMD_SET_DISP_OFFSET    0xD3
#define SSD1306_CMD_SET_CLK_DIV        0xD5
#define SSD1306_CMD_CHARGE_PUMP        0x8D
#define SSD1306_CMD_ENTIRE_DISPLAY_ON  0xA5
#define SSD1306_CMD_ENTIRE_DISPLAY_OFF 0xA4

extern SPI_HandleTypeDef OLED_SPI_HANDLE;

/* ── 双缓冲 + 状态机 ─────────────────────────────────────────── */
static uint8_t oled_buf_a[OLED_BUFFER_SIZE];
static uint8_t oled_buf_b[OLED_BUFFER_SIZE];
uint8_t *OLED_Buffer = oled_buf_a;          /* 绘制缓冲（CPU 写） */
static uint8_t *oled_send_buf = oled_buf_b; /* 发送缓冲（DMA 读） */
static volatile int oled_new_frame = 0;
static volatile int oled_dma_busy = 0;      /* 0=空闲态  1=传输态 */

/* ── 内部函数 ───────────────────────────────────────────────────── */

static void OLED_WriteCommand(uint8_t cmd)
{
    OLED_DC_LOW();
    HAL_SPI_Transmit(&OLED_SPI_HANDLE, &cmd, 1, HAL_MAX_DELAY);
}

static void OLED_Reset(void)
{
    OLED_RST_LOW();
    HAL_Delay(10);
    OLED_RST_HIGH();
    HAL_Delay(100);
}

/* 发送列/页命令 + 启动 DMA 数据传输
 * CS 永久低电平（硬件接地），无需软件管理 */
static void oled_start_dma(void)
{
    OLED_DC_LOW();
    OLED_WriteCommand(SSD1306_CMD_SET_COL_ADDR);
    OLED_WriteCommand(0x00);
    OLED_WriteCommand(OLED_WIDTH - 1);

    OLED_WriteCommand(SSD1306_CMD_SET_PAGE_ADDR);
    OLED_WriteCommand(0x00);
    OLED_WriteCommand((OLED_HEIGHT / 8) - 1);

    /* 等待 SPI 完全空闲后再切换 DC */
    while (__HAL_SPI_GET_FLAG(&OLED_SPI_HANDLE, SPI_FLAG_BSY)) {}

    OLED_DC_HIGH();

    if (OLED_SPI_HANDLE.State == HAL_SPI_STATE_READY) {
        oled_dma_busy = 1;
        if (HAL_SPI_Transmit_DMA(&OLED_SPI_HANDLE, oled_send_buf, OLED_BUFFER_SIZE) != HAL_OK) {
            oled_dma_busy = 0;
        }
    }
}

/* ── 状态机 ────────────────────────────────────────────────────── */

/*
 * 空闲态（DMA 停止）：
 *   20Hz ISR → 渲染到绘制缓冲 → OLED_SendBuffer 设标志
 *   主循环检测标志+SPI空闲 → OLED_SwapAndStart → 传输态
 *
 * 传输态（DMA 发送中）：
 *   20Hz ISR → 渲染 → OLED_SendBuffer 设标志（不碰 DMA）
 *   TC 回调：
 *     有新帧 → 交换缓冲 → 重启 DMA → 保持传输态
 *     无新帧 → 标记空闲 → 回空闲态
 */

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance != OLED_SPI_HANDLE.Instance)
        return;

    /* 不交换缓冲，只标记 DMA 空闲
     * 交换由主循环 SwapAndStart 独占执行，避免回调与 ISR 渲染竞争 */
    oled_dma_busy = 0;
}

/* ── 初始化 ─────────────────────────────────────────────────────── */

void OLED_Init(void)
{
    OLED_Reset();

    OLED_WriteCommand(SSD1306_CMD_DISPLAY_OFF);
    OLED_WriteCommand(SSD1306_CMD_SET_CLK_DIV);
    OLED_WriteCommand(0x80);
    OLED_WriteCommand(SSD1306_CMD_SET_MUX_RATIO);
    OLED_WriteCommand(0x3F);
    OLED_WriteCommand(SSD1306_CMD_SET_DISP_OFFSET);
    OLED_WriteCommand(0x00);
    OLED_WriteCommand(SSD1306_CMD_SET_DISP_START_LINE);
    OLED_WriteCommand(SSD1306_CMD_CHARGE_PUMP);
    OLED_WriteCommand(0x14);
    OLED_WriteCommand(SSD1306_CMD_SET_MEM_ADDR_MODE);
    OLED_WriteCommand(0x00);
    OLED_WriteCommand(SSD1306_CMD_SET_SEGMENT_REMAP | 0x01);
    OLED_WriteCommand(SSD1306_CMD_SET_COM_SCAN_DIR | 0x08);
    OLED_WriteCommand(SSD1306_CMD_SET_COM_PIN_CFG);
    OLED_WriteCommand(0x12);
    OLED_WriteCommand(SSD1306_CMD_SET_CONTRAST);
    OLED_WriteCommand(0x7F);
    OLED_WriteCommand(SSD1306_CMD_SET_PRECHARGE);
    OLED_WriteCommand(0xF1);
    OLED_WriteCommand(SSD1306_CMD_SET_VCOM_DESEL);
    OLED_WriteCommand(0x30);
    OLED_WriteCommand(SSD1306_CMD_ENTIRE_DISPLAY_OFF);
    OLED_WriteCommand(SSD1306_CMD_DISPLAY_ON);

    HAL_Delay(100);

    /* 初始帧：清屏 → 拷贝到发送缓冲 → 启动首次 DMA → 进入传输态 */
    OLED_Buffer = oled_buf_a;
    oled_send_buf = oled_buf_b;
    OLED_Clear();
    memcpy(oled_send_buf, OLED_Buffer, OLED_BUFFER_SIZE);
    oled_start_dma();
}

/* ── 显存操作（只写绘制缓冲） ──────────────────────────────────── */

void OLED_Clear(void)
{
    uint32_t i;
    for (i = 0; i < OLED_BUFFER_SIZE; i++)
        OLED_Buffer[i] = 0x00;
}

void OLED_SetPixel(uint8_t x, uint8_t y, uint8_t state)
{
    if (x >= OLED_WIDTH || y >= OLED_HEIGHT) return;
    uint8_t page = y / 8;
    uint8_t bit = y % 8;
    if (state == OLED_PIXEL_ON)
        OLED_Buffer[page * OLED_WIDTH + x] |= (1 << bit);
    else
        OLED_Buffer[page * OLED_WIDTH + x] &= ~(1 << bit);
}

void OLED_ShowString(uint8_t x, uint8_t y, const char* str)
{
    OLED_ShowStringEx(x, y * 16, str, FONT_LARGE);
}

void OLED_ShowStringEx(uint8_t x, uint8_t y, const char* str, FontType font)
{
    if (str == NULL) return;

    uint8_t font_width, font_height;
    if (font == FONT_SMALL) {
        font_width = FONT_4X6_WIDTH;
        font_height = FONT_4X6_HEIGHT;
    } else if (font == FONT_MEDIUM) {
        font_width = FONT_6X10_WIDTH;
        font_height = FONT_6X10_HEIGHT;
    } else {
        font_width = FONT_WIDTH;
        font_height = FONT_HEIGHT;
    }

    uint8_t base_page = y / 8;
    uint8_t base_bit = y % 8;

    while (*str != '\0') {
        uint8_t char_index = (uint8_t)*str;
        uint8_t row;
        for (row = 0; row < font_height; row++) {
            uint8_t bit = base_bit + row;
            uint8_t page = base_page + (bit / 8);
            bit = bit % 8;

            uint8_t data;
            if (font == FONT_SMALL)
                data = Font4x6[char_index * font_height + row];
            else if (font == FONT_MEDIUM)
                data = Font6x10[char_index * font_height + row];
            else
                data = Font8x16[char_index * font_height + row];

            if (page >= OLED_HEIGHT / 8) continue;

            uint8_t col;
            for (col = 0; col < font_width; col++) {
                uint8_t px_x = x + col;
                if (px_x >= OLED_WIDTH) continue;
                if (data & (1 << (font_width - 1 - col)))
                    OLED_Buffer[page * OLED_WIDTH + px_x] |= (1 << bit);
                else
                    OLED_Buffer[page * OLED_WIDTH + px_x] &= ~(1 << bit);
            }
        }
        x += font_width;
        str++;
    }
}

/* ── 帧提交接口 ─────────────────────────────────────────────────── */

void OLED_SendBuffer(void)
{
    /* ISR 调用：只设标志，绝不碰 SPI/DMA */
    oled_new_frame = 1;
}

int OLED_IsNewFrame(void)
{
    return oled_new_frame;
}

void OLED_SwapAndStart(void)
{
    if (!OLED_IsReady()) {
        return;
    }

    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    oled_new_frame = 0;
    uint8_t *tmp = OLED_Buffer;
    OLED_Buffer = oled_send_buf;
    oled_send_buf = tmp;

    if (!primask) {
        __enable_irq();
    }

    oled_start_dma();
}

int OLED_IsReady(void)
{
    return (oled_dma_busy == 0) && (OLED_SPI_HANDLE.State == HAL_SPI_STATE_READY);
}

void OLED_TestFullScreen(void)
{
    uint32_t i;
    for (i = 0; i < OLED_BUFFER_SIZE; i++)
        OLED_Buffer[i] = 0xFF;
    OLED_SendBuffer();
}
