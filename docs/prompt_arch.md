# DC-AC 全桥逆变器 开发提示词 — 架构篇

## 声明

你是一个嵌入式开发 AI Coder。本项目采用严格四层架构，你必须理解并遵守以下所有规则后再开始写代码。**禁止一次性输出全部代码，必须一层一层实现。**

---

## 一、四层架构

```
port/  ── 移植层  : 封装 STM32 HAL，换芯片只改这层
algo/  ── 算法层  : 纯 float 数学，零硬件依赖，可 PC 单测
app/   ── 编排层  : 把 port/ 和 algo/ 串起来
Core/  ── CubeMX  : 生成代码不动，只碰 USER CODE 区
```

**三层分工口诀：**
- `port/` : "我知道芯片长什么样，不知道逆变器是什么"
- `algo/` : "我知道怎么算 SPWM，不知道芯片在哪"
- `app/`  : "我知道怎么把算法和硬件串起来，但不认识寄存器名"

---

## 二、已有外设与驱动（不改动，只调用）

以下模块已由 CubeMX 配置或已有驱动代码，**port/ 和 app/ 只调其接口，不改其内部**：

| 外设 | 位置 | 说明 |
|------|------|------|
| TIM8 PWM | CubeMX `Core/Src/tim.c` | 20kHz CH1/1N/2/2N，极性 LOW，死区 10 |
| ADC1 DMA | CubeMX `Core/Src/adc.c` | TIM3_TRGO 触发 10kHz，DMA 半字搬运 |
| USART2 | CubeMX `Core/Src/usart.c` | 115200 8N1，DMA TX/RX 已配置 |
| SPI1 | CubeMX `Core/Src/spi.c` | 已配 |
| OLED | `oled_driver/` | SSD1306 SPI，已有基础画点/字符串 API，**不要改** |
| 按键 | CubeMX `Core/Src/gpio.c` | KEY0=PC5, KEY1=PA15，上拉，按下为低 |
| LED | CubeMX `Core/Src/gpio.c` | LED0=PA8, LED1=PD2，开漏 |

**port/ 层直接引用 CubeMX 生成的 `extern TIM_HandleTypeDef htim8` 等句柄，调 HAL API。**

---

## 三、Core/ 目录铁律

### 可以碰的
- `main.c` 的 `/* USER CODE BEGIN 2 */` 区 — 只放运行时启动调用
- `stm32f1xx_it.c` 的 `/* USER CODE BEGIN */` 区 — 中断回调转发
- `CMakeLists.txt` — 只加源文件路径

### 绝对禁止
- 修改 `USER CODE` 注释块之外的任何代码
- 修改 `MX_xxx_Init()` 函数
- 在 `main.c` 的非 USER CODE 区写业务逻辑

### main.c USER CODE 2 的唯一内容
```c
/* USER CODE BEGIN 2 */
pwm_start();
adc_start_dma();
timer_start_isr();
/* USER CODE END 2 */
```
CubeMX 已经调完了所有 `MX_xxx_Init()`，这里只做运行时启动。

---

## 四、port/ 移植层规范

### 核心思想

port/ 是 CubeMX 与业务代码之间的**运行时桥梁**。CubeMX 配好了所有外设参数（时钟、GPIO、NVIC、DMA、通道配置），port/ 只按"启动/停止/读/写"按钮。

### 核心规则

1. **不做 init** — CubeMX 已在 `main()` 调完所有 `MX_xxx_Init()`，port/ 不调任何 Init 函数
2. **不做 config** — port/ 不调 `HAL_xxx_Config*()`、不用配置类宏 `__HAL_xxx_SET_PRESCALER` 等
3. **不硬编码 CubeMX 参数** — ARR/PSC/波特率等从 handle 读（如 `__HAL_TIM_GET_AUTORELOAD`），不写 `#define PWM_ARR 799`
4. **每个外设一个 `.c` + `.h`**
5. **暴露功能接口，隐藏寄存器** — 上层只调 `pwm_ch1_set(0.5)`，不知道 CCR/ARR
6. **内部只用 HAL 业务函数** — 不直接写寄存器，不用配置类 HAL 宏

### 允许使用 vs 禁止使用

| ✅ 允许（业务函数） | ❌ 禁止（配置函数/宏） |
|---|---|
| `HAL_TIM_PWM_Start/Stop` | `HAL_TIM_PWM_ConfigChannel`, `HAL_TIM_Base_Init` |
| `HAL_TIMEx_PWMN_Start/Stop` | `HAL_TIMEx_ConfigBreakDeadTime`, `MX_xxx_Init` |
| `HAL_TIM_Base_Start_IT` | `HAL_TIM_ConfigClockSource`, `HAL_xxx_MspInit` |
| `HAL_ADC_Start_DMA` | `HAL_ADC_Init`, `HAL_ADC_ConfigChannel` |
| `HAL_UART_Receive_IT` | `HAL_UART_Init`, `__HAL_UART_ENABLE_IT` |
| `HAL_UART_Transmit_DMA` | `HAL_NVIC_SetPriority/EnableIRQ`, `HAL_DMA_Init` |
| `HAL_GPIO_ReadPin` | `HAL_GPIO_Init`, `TIMx->`, `GPIOx->` 直接寄存器 |
| `__HAL_TIM_SET_COMPARE`（运行时数据） | `__HAL_TIM_SET_PRESCALER/AUTORELOAD`（配置参数） |
| `__HAL_TIM_GET_AUTORELOAD`（读参数） | `__HAL_TIM_CLEAR_FLAG`（配置操作） |
| HAL weak 回调覆写 | `#define PWM_ARR 799` 等硬编码 CubeMX 参数 |

### 文件清单

| 文件 | 职责 |
|------|------|
| `port/pwm.c` | TIM8 CH1/1N/2/2N 全桥 PWM：`pwm_start()` / `pwm_chX_set(float duty)` |
| `port/adc.c` | ADC1 DMA 采样 + 50 深环形缓冲：`adc_start_dma()` / `adc_get_batch(float*,n)` |
| `port/uart.c` | USART2：RX 字节中断拼行 / TX DMA NORMAL 发送：`uart_read_line()` / `uart_send()` |
| `port/timer.c` | TIM3/6/7 中断启动 + ISR 回调转发：`timer_start_isr()` |
| `port/key.c` | 按键读取 + 去抖：`key_scan()` 返回 KEY0_SHORT/KEY1_SHORT/KEY_NONE |

### 对上层隐藏的内容（检验标准）

```
port/pwm : 隐藏 CCxE/CCxNE/MOE、CCR 寄存器、ARR 值
port/adc : 隐藏 raw ADC 值、DMA buffer 地址、中心电压值
port/uart: 隐藏 DMA 通道、buf 地址、中断标志、波特率
port/timer: 隐藏 TIM 句柄、NVIC 优先级
port/key : 隐藏 GPIO 引脚号、有效电平、去抖次数
```

### 关键实现要点

**port/pwm.c:**
- `pwm_start()` 内部调 `HAL_TIM_PWM_Start(CH1)` / `HAL_TIM_PWM_Start(CH2)` / `HAL_TIMEx_PWMN_Start(CH1)` / `HAL_TIMEx_PWMN_Start(CH2)`
- `pwm_chX_set(float duty)` 内部：ARR 从 `__HAL_TIM_GET_AUTORELOAD(&htim8)` 读取（**不硬编码**），`CCR = clamp(duty * arr, arr*5%, arr*95%)` → `__HAL_TIM_SET_COMPARE`
- CubeMX 已配好极性(LOW)和死区(10)，这里只做运行时使能

**port/uart.c:**
- RX: `HAL_UART_Receive_IT` 单字节中断，逐字拼行，遇 `\n` 标记就绪
- TX: `HAL_UART_Transmit_DMA` NORMAL 模式，`tx_busy` 标志防重发
- **不用 UART IDLE 中断，不用 ring buffer**

**port/adc.c:**
- 50 深环形缓冲，`HAL_ADC_ConvCpltCallback` 覆写做 raw→float V 快照
- `adc_get_batch()` 关中断拷贝快照，按从旧到新取最近 50 个

**port/timer.c:**
- `timer_start_isr()` **只调** `HAL_TIM_Base_Start_IT` ×3，不做 `__HAL_TIM_CLEAR_FLAG`（HAL 内部已处理）
- `HAL_TIM_PeriodElapsedCallback` 放在 `stm32f1xx_it.c` 的 USER CODE 区，**不在 port/ 里实现**

**port/key.c:**
- 读 PC5(KEY0) / PA15(KEY1)，连续 N 次一致才确认，输出事件

---

## 五、algo/ 算法层规范

### 核心规则
1. **纯 C，零硬件依赖** — 不含任何 `#include "stm32f1xx_hal.h"` 或 `port/xxx.h`
2. **输入输出全 float**
3. **可在 PC 上用 gcc 编译单测**
4. **每个模块 `.c` + `.h`**

### 文件清单

| 文件 | 职责 | 算法卷章节 |
|------|------|-----------|
| `algo/phase_gen.c` | 相位累加器 + 200 点正弦表，40-70Hz 可变 | 算法篇 第三章 |
| `algo/spwm.c` | 单极性 3 电平全桥 SPWM，mod+sinθ → duty_a/b | 算法篇 第五章 |
| `algo/sogi.c` | 二阶广义积分器，50 样本批量迭代 → vAlpha/vBeta | 算法篇 第四章 |
| `algo/pi.c` | PI 控制器 + 抗积分饱和 | 算法篇 第四章 |
| `algo/park.c` | Park 变换，vAlpha/vBeta + cos/sin → vD | 算法篇 第四章 |

### 检验标准

```
grep -r "HAL\|TIM\d\|CCR\|GPIO\|NVIC\|DMA\|__HAL" algo/
# 必须返回空 — 搜不到任何一个硬件关键词
```

---

## 六、app/ 编排层规范

### 核心规则
1. **只调 `port/xxx.h` 和 `algo/xxx.h` 的接口**
2. **不直接写寄存器** — 禁止 `__HAL_TIM_SET_COMPARE` 等
3. **ISR 级代码放这里，非 ISR 代码也放这里**

### 文件清单

| 文件 | 职责 |
|------|------|
| `app/control.c` | ISR 级控制循环（10kHz + 200Hz） |
| `app/params.c` | 影子寄存器管理，`apply_params()` 统一生效 |
| `app/panel.c` | 主循环：按键映射、UART SET 解析、OLED 刷新、状态上报 |

### app/control.c ISR 调度流

```
10kHz ISR (100μs):
  1. modulation_active = params_shadow.modulation
  2. phase_advance()
  3. sin = phase_sin()
  4. spwm_compute(modulation_active, sin, &duty_a, &duty_b)
  5. pwm_ch1_set(duty_a)
  6. pwm_ch2_set(duty_b)

200Hz ISR (5ms):
  1. uart_poll()          // port/uart 内部轮询 DMA
  2. adc_get_batch(buf, 50)
  3. sogi_process_batch(sogi, buf, &vAlpha, &vBeta)
  4. cos = phase_cos(), sin = phase_sin()
  5. vD = park_vd(vAlpha, vBeta, cos, sin)
  6. modulation = pi_update(pi, params.vref, vD)
  7. params_shadow.modulation = modulation
  8. apply_params()       // 如果 paramsPending，在此统一生效
```

### app/params.c 参数机制

- `params_shadow` : 由 panel 写入（按键/UART SET），只写影子，不直接 Apply
- `params_active`  : 生效参数
- `apply_params()`  : 由 200Hz ISR 统一调用，原子替换 → 调 `phase_set_freq()` / `pi_set_gains()`
- **UART SET 命令不直接 Apply，只写影子 + 置 pending 标志**

### app/panel.c 主循环

```
主循环 (while 1):
  if 20Hz_flag: key_scan() → KEY0: vref+step, KEY1: vref-step → 写 params_shadow
  if uart_read_line(line): 解析 SET P:/I:/V:/F: → 写 params_shadow，置 pending
  OLED 刷新: Vref / Vrms / Mod / Freq
  uart_send() 定时上报状态
```

---

## 七、中断向量挂载（stm32f1xx_it.c USER CODE 区）

```c
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM7)  app_on_10khz();      // 10kHz 直接干
    if (htim->Instance == TIM6)  app_on_200hz();      // 200Hz 直接干
    if (htim->Instance == TIM3)  g_flag_20hz = 1;     // 20Hz 只置标志，主循环处理
}

// USART2 RXNE 中断
void USART2_IRQHandler(void)
{
    if (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_RXNE)) {
        uart_rx_isr();   // 在 port/uart.c 中实现
    }
    HAL_UART_IRQHandler(&huart2);
}
```

---

## 八、执行顺序

1. **先给 `port/` 各模块的头文件** — 定义接口
2. **实现 `algo/`** — 纯数学，含 PC 端单测
3. **实现 `port/` 各模块** — 调 HAL，实现第一步的接口
4. **实现 `app/`** — 编排 port 和 algo
5. **挂载 Core/** — main.c + stm32f1xx_it.c + CMakeLists

**每一层做完，独立编译通过，再进入下一层。**
