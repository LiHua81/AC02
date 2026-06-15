# SSD1306 OLED 驱动说明书

## 一、概述

本驱动为 STM32F103 平台设计，支持 SSD1306 128x64 OLED 显示屏，采用 4 线 SPI 接口（CS 引脚硬件接地，软件无需控制）。集成 TIM6 定时器实现 20Hz 定时刷新。



## 三、快速开始

### 3.1 初始化

```c
TIM6_Init();   // 开启20Hz定时器中断
OLED_Init();   // 初始化OLED
```

### 3.2 基本操作

```c
OLED_Clear();                          // 清屏
OLED_ShowString(0, 0, "Hello World");  // 显示大字体字符串
OLED_SendBuffer();                     // 刷新到屏幕
```

### 3.3 主循环

```c
while (1) {
    if (TIM6_Refresh_Flag) {
        TIM6_Refresh_Flag = 0;
        OLED_SendBuffer();  // 50ms自动刷新
    }
}
```

## 四、字体支持

### 4.1 字体规格

| 字体类型 | 尺寸 | 说明 |
|----------|------|------|
| `FONT_SMALL` | 4×6 | 小字体，适合显示大量信息 |
| `FONT_MEDIUM` | 6×10 | 中等字体，平衡大小和可读性 |
| `FONT_LARGE` | 8×16 | 大字体，适合标题和重要信息 |

### 4.2 使用不同字体

```c
// 使用小字体
OLED_ShowStringEx(0, 0, "Small Font", FONT_SMALL);

// 使用中等字体
OLED_ShowStringEx(0, 10, "Medium Font", FONT_MEDIUM);

// 使用大字体
OLED_ShowStringEx(0, 22, "Large Font", FONT_LARGE);
```

## 五、API 参考

### 5.1 核心函数

| 函数 | 功能 |
|------|------|
| `OLED_Init()` | 初始化OLED |
| `OLED_Clear()` | 清空显存 |
| `OLED_SetPixel(x, y, state)` | 设置像素点 |
| `OLED_SendBuffer()` | 刷新显存到屏幕 |
| `TIM6_Init()` | 初始化定时器中断 |

### 5.2 字符串显示函数

| 函数 | 功能 |
|------|------|
| `OLED_ShowString(x, y, str)` | 显示大字体字符串（兼容旧版） |
| `OLED_ShowStringEx(x, y, str, font)` | 显示字符串（可选择字体） |

### 5.3 参数说明

| 参数 | 说明 | 范围 |
|------|------|------|
| `x` | X坐标（像素） | 0 ~ 127 |
| `y` | Y坐标（像素） | 0 ~ 63 |
| `str` | 字符串指针 | NULL终止 |
| `font` | 字体类型 | FONT_SMALL/MEDIUM/LARGE |


## 七、测试程序

### 7.2 测试代码

```c
void DisplayFontDemo(void) {
    OLED_Clear();
    OLED_ShowStringEx(0, 0, "FONT LARGE (8x16)", FONT_LARGE);
    OLED_ShowStringEx(0, 20, "Font Medium (6x10):", FONT_MEDIUM);
    OLED_ShowStringEx(0, 32, "Test String", FONT_MEDIUM);
    OLED_ShowStringEx(0, 48, "Font Small (4x6):", FONT_SMALL);
    OLED_ShowStringEx(0, 56, "Hello World!", FONT_SMALL);
    OLED_SendBuffer();
}
```

## 八、技术参数

- **屏幕分辨率**: 128 × 64
- **接口方式**: 4线SPI（无CS控制）
- **刷新频率**: 20Hz（50ms）
- **显存大小**: 1024 字节
- **字体规格**: 4×6、6×10、8×16 三种
