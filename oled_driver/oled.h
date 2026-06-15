/**
  ******************************************************************************
  * @file    oled.h
  * @author  Driver Generator
  * @brief   SSD1306 128x64 OLED 4线SPI驱动头文件
  * 
  * @note    CS引脚硬件接地，软件无需控制
  * @note    硬件层与逻辑层完全解耦，移植仅需修改宏定义
  * @version V1.1.0
  * @date    2024-01-01
  ******************************************************************************
  */

#ifndef __OLED_H
#define __OLED_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"

/* Exported types ------------------------------------------------------------*/

/**
 * @brief 字体类型枚举
 */
typedef enum {
    FONT_SMALL,     /* 4x6 小字体 */
    FONT_MEDIUM,    /* 6x10 中等字体 */
    FONT_LARGE      /* 8x16 大字体 */
} FontType;

/* Exported constants --------------------------------------------------------*/

/**
 * @brief OLED屏幕参数宏定义
 */
#define OLED_WIDTH                    128    /* 屏幕宽度 */
#define OLED_HEIGHT                   64     /* 屏幕高度 */
#define OLED_BUFFER_SIZE              ((OLED_WIDTH * OLED_HEIGHT) / 8)  /* 显存大小: 1024字节 */

/**
 * @brief 硬件引脚宏定义 - 修改此处适配不同硬件
 */
#define OLED_DC_PIN                   GPIO_PIN_4    /* DC引脚 */
#define OLED_DC_PORT                  GPIOB         /* DC端口 */
#define OLED_RST_PIN                  GPIO_PIN_6    /* RST引脚 */
#define OLED_RST_PORT                 GPIOB         /* RST端口 */

/**
 * @brief SPI句柄宏定义 - 修改此处适配不同SPI
 */
#define OLED_SPI_HANDLE               hspi1        /* SPI句柄名称 */

/**
 * @brief 像素状态定义
 */
#define OLED_PIXEL_OFF                0    /* 像素关闭 */
#define OLED_PIXEL_ON                 1    /* 像素开启 */

/* Exported macro ------------------------------------------------------------*/

/**
 * @brief DC引脚控制宏
 */
#define OLED_DC_HIGH()                HAL_GPIO_WritePin(OLED_DC_PORT, OLED_DC_PIN, GPIO_PIN_SET)
#define OLED_DC_LOW()                 HAL_GPIO_WritePin(OLED_DC_PORT, OLED_DC_PIN, GPIO_PIN_RESET)

/**
 * @brief RST引脚控制宏
 */
#define OLED_RST_HIGH()               HAL_GPIO_WritePin(OLED_RST_PORT, OLED_RST_PIN, GPIO_PIN_SET)
#define OLED_RST_LOW()                HAL_GPIO_WritePin(OLED_RST_PORT, OLED_RST_PIN, GPIO_PIN_RESET)

/* Exported variables --------------------------------------------------------*/

extern uint8_t *OLED_Buffer;  /* 绘制缓冲指针（双缓冲，指向当前可写缓冲） */

/* Exported functions prototypes ---------------------------------------------*/

/**
 * @brief  OLED初始化函数
 * @note   执行标准SSD1306初始化序列，配置显示参数
 * @param  None
 * @retval None
 */
void OLED_Init(void);

/**
 * @brief  清屏函数
 * @note   将显存缓冲区全部清零
 * @param  None
 * @retval None
 */
void OLED_Clear(void);

/**
 * @brief  设置像素点
 * @note   在指定坐标设置像素状态
 * @param  x: X坐标 (0 ~ OLED_WIDTH-1)
 * @param  y: Y坐标 (0 ~ OLED_HEIGHT-1)
 * @param  state: 像素状态 (OLED_PIXEL_ON/OLED_PIXEL_OFF)
 * @retval None
 */
void OLED_SetPixel(uint8_t x, uint8_t y, uint8_t state);

/**
 * @brief  显示英文字符串（使用默认8x16字体）
 * @note   使用8x16字体显示字符串
 * @param  x: 起始X坐标
 * @param  y: 起始Y坐标（行号，0~7，每行16像素）
 * @param  str: 字符串指针
 * @retval None
 */
void OLED_ShowString(uint8_t x, uint8_t y, const char* str);

/**
 * @brief  显示英文字符串（可选择字体）
 * @note   支持4x6小字体和8x16大字体
 * @param  x: 起始X坐标
 * @param  y: 起始Y坐标（像素）
 * @param  str: 字符串指针
 * @param  font: 字体类型 (FONT_SMALL/FONT_LARGE)
 * @retval None
 */
void OLED_ShowStringEx(uint8_t x, uint8_t y, const char* str, FontType font);

/**
 * @brief  刷新显存到屏幕
 * @note   将内部缓冲区数据发送到OLED屏幕
 * @param  None
 * @retval None
 */
void OLED_SendBuffer(void);
int  OLED_IsNewFrame(void);
void OLED_SwapAndStart(void);
int  OLED_IsReady(void);

/**
 * @brief  全屏点亮测试
 * @note   用于测试OLED是否能正常显示，全亮屏幕
 * @param  None
 * @retval None
 */
void OLED_TestFullScreen(void);

#ifdef __cplusplus
}
#endif

#endif /* __OLED_H */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/