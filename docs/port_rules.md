# Port 层规范：实现什么、怎么避免错误

## 一、问题回顾：port 层到底出了哪些错

| 错误 | 本质 | 后果 |
|------|------|------|
| PWM 硬编码 ARR=799 | **重复了 CubeMX 的配置** | CubeMX 改了 port 没改，占空比错 |
| Timer 用 `__HAL_TIM_CLEAR_FLAG` | **做了 HAL 已经做过的事** | 多余操作，可能清掉有效标志 |
| ADC `__disable_irq` 嵌套 | **port 内部做了 app 层的临界区管理** | 破坏外层保护，SOGI 崩溃 |
| UART 混搭 Receive_IT + Transmit_DMA | **不理解 HAL 状态机** | RX 占住 RxState，TX 被永久拒绝 |
| UART tx_busy 竞态 | **memcpy 和 flag 设置之间无保护** | DMA TC 抢占清 flag，数据错乱 |

**共同根因：port 层做了不该做的事。**

---

## 二、Port 层到底实现什么

### port 的唯一职责

> **把 CubeMX 配置好的外设，以"运行时操作"的方式暴露给 app 层。**

CubeMX 管配置（PSC、ARR、GPIO、NVIC、DMA 通道），port 管运行时（启动、停止、读、写、回调）。

### port 该做的

```
1. 启动/停止外设
   pwm_start()        → HAL_TIM_PWM_Start × 4
   adc_start_dma()    → HAL_ADC_Start_DMA
   uart_init()        → HAL_UART_Receive_DMA + 开 IDLE 中断
   timer_start_isr()  → HAL_TIM_Base_Start_IT × 3

2. 运行时数据读写
   pwm_chX_set(duty)  → __HAL_TIM_SET_COMPARE（写运行时数据，非配置）
   uart_send(str)     → HAL_UART_Transmit_DMA
   uart_read_line()   → 从行缓冲取一行
   adc_get_batch()    → 从 DMA 快照取数据
   key_scan()         → HAL_GPIO_ReadPin

3. HAL 回调覆写（ISR 上下文）
   HAL_ADC_ConvCpltCallback    → raw→float 快照
   HAL_UART_TxCpltCallback     → tx_busy = 0
   HAL_UART_ErrorCallback      → 恢复 DMA
   uart_on_idle()              → DMA 缓冲区行装配
```

### port 不该做的

```
✗ 重复 CubeMX 的配置
  不要硬编码 ARR、PSC、波特率等 CubeMX 已经设好的值
  需要时从 handle 读：__HAL_TIM_GET_AUTORELOAD(&htim8)

✗ 做 HAL 已经做过的事
  不要手动清标志、手动使能外设、手动配 GPIO
  HAL_Start 函数内部已经处理了

✗ 管理临界区
  port 函数内部禁止 __disable_irq / __enable_irq
  临界区由 app 层调用方统一决定

✗ 混搭 HAL 模式
  同一个外设的所有操作必须用同一类 API：
    全 DMA、全 IT、或全 Polling
  禁止：RX 用 IT + TX 用 DMA

✗ 包含 app/ 或 algo/ 的头文件
  port 不认识业务逻辑
```

---

## 三、每个 port 模块的实现清单

### port_pwm.c（~40 行）

```
该做：
  pwm_start()     — 4 路 HAL_TIM_PWM_Start / PWMN_Start
  pwm_stop()      — 4 路 Stop
  pwm_chX_set()   — __HAL_TIM_GET_AUTORELOAD 读 ARR
                     clamp 5%~95%
                     __HAL_TIM_SET_COMPARE 写 CCR

不该做：
  不要 #define PWM_ARR
  不要配极性、死区、频率（CubeMX 配了）
  不要调 HAL_TIM_PWM_ConfigChannel
```

### port_adc.c（~40 行）

```
该做：
  adc_start_dma()      — HAL_ADC_Start_DMA（DMA circular 自动触发）
  HAL_ADC_ConvCpltCallback — raw→float 快照，设 ready 标志
  adc_get_batch()      — 拷贝快照到调用方 buffer
  adc_read_v()         — 返回最新单值

不该做：
  函数内部禁止 __disable_irq / __enable_irq
  不要配 ADC 通道、采样时间、触发源（CubeMX 配了）
  不要调 HAL_ADC_ConfigChannel
```

### port_uart.c（~110 行）

```
该做：
  uart_init()           — HAL_UART_Receive_DMA + 开 IDLE 中断
                           + NVIC SetPriority + EnableIRQ
  uart_send()           — tx_busy 检查（临界区保护）
                           + HAL_UART_Transmit_DMA
  uart_read_line()      — 从行缓冲取一行
  uart_on_idle()        — 读 DMA counter，处理新字节，装配行
  HAL_UART_TxCpltCallback — tx_busy = 0
  HAL_UART_ErrorCallback  — 恢复 DMA，清 tx_busy

不该做：
  不要混搭 Receive_IT 和 Transmit_DMA（必须全 DMA）
  不要 HalfCplt/Cplt 回调处理数据（只用 IDLE）
  不要配波特率、字长（CubeMX 配了）
  不要调 HAL_UART_Init

例外：
  uart_send 内的 __disable_irq 是必要的。tx_busy 由 DMA TC 回调（更高优先级）
  清零，如果 memcpy 与 tx_busy=1 之间被抢占，DMA 会写入正在拷贝的缓冲区。
  必须在注释中标注此例外的原因。

  uart_init 内的 HAL_NVIC_SetPriority/EnableIRQ 是必要的。CubeMX 为 DMA 模式
  配置 DMA 通道中断，但不配置 USART2 外设中断（IDLE 检测需要它）。
```

### port_timer.c（~12 行）

```
该做：
  timer_start_isr() — HAL_TIM_Base_Start_IT × 3

不该做：
  不要 __HAL_TIM_CLEAR_FLAG（HAL 内部已处理）
  不要配 PSC、ARR（CubeMX 配了）
  不要 HAL_TIM_Base_Init
  不要 HAL_NVIC_SetPriority（CubeMX MspInit 已配）
```

### port_key.c（~30 行）

```
该做：
  key_scan() — HAL_GPIO_ReadPin + 连续 N 次确认

不该做：
  不要 HAL_GPIO_Init（CubeMX 配了）
  不要配引脚模式、上下拉（CubeMX 配了）
```

---

## 四、防错检查表

写完每个 port 模块后，逐条检查：

### 4.1 禁止项检查

- [ ] 没有 `#define` 任何 CubeMX 已配的参数值（ARR、PSC、波特率等）
- [ ] 没有 `HAL_xxx_Init` / `HAL_xxx_Config*` / `HAL_xxx_MspInit` 调用
- [ ] 没有 `__HAL_xxx_SET_PRESCALER` / `__HAL_TIM_CLEAR_FLAG` 等配置类宏
- [ ] 没有 `TIMx->` / `GPIOx->` / `ADCx->` 直接寄存器访问（`__HAL_TIM_SET_COMPARE` 例外）
- [ ] 没有 `HAL_GPIO_Init` / `HAL_NVIC_*`（`uart_init` 中的 `NVIC_SetPriority` 例外，因为 CubeMX 不会为 IT 模式配 NVIC）
- [ ] 没有 `__disable_irq` / `__enable_irq`（例外：`uart_send` 的 `tx_busy` 原子保护，需注释说明原因）
- [ ] 没有 `#include "app/xxx.h"` / `#include "algo/xxx.h"`
- [ ] 同一外设没有混搭 DMA/IT/Polling 模式

### 4.2 正面项检查

- [ ] 每个 `extern` handle 都能在 Core/Src/ 中找到定义
- [ ] ARR 等运行时需要的参数从 handle 读取
- [ ] HAL 回调函数检查了 `huart->Instance == USARTx` 等多实例保护
- [ ] 函数命名统一：`xxx_init` / `xxx_start` / `xxx_read` / `xxx_write`

---

## 五、HAL 状态机速查

这是 port 层最容易踩的坑。HAL 的 handle 里有 `gState` 和 `RxState` 两个状态变量：

```
HAL_UART_Receive_IT()    → RxState = BUSY_RX
HAL_UART_Receive_DMA()   → gState  = BUSY_RX
HAL_UART_Transmit_IT()   → gState  = BUSY_TX
HAL_UART_Transmit_DMA()  → gState  = BUSY_TX
```

**`HAL_UART_Transmit_DMA` 的前置检查：**
```c
if ((huart->gState == HAL_UART_STATE_READY) &&
    (huart->RxState == HAL_UART_STATE_READY || ...))
```

如果 RxState 被 Receive_IT 设为 BUSY_RX → Transmit_DMA 被拒绝。

**规则：同一外设，TX 和 RX 必须用同一类 API。**

| 组合 | gState | RxState | 能否工作 |
|------|--------|---------|---------|
| RX_DMA + TX_DMA | ✓ 独立 | ✓ 独立 | ✓ |
| RX_IT + TX_IT | ✓ 独立 | ✓ 独立 | ✓ |
| RX_DMA + TX_IT | ✗ 冲突 | ✓ | ✗ |
| RX_IT + TX_DMA | ✓ | ✗ 冲突 | ✗ |

---

## 六、port 层的本质

port 不是"驱动层"，不需要实现协议、管理状态、处理错误恢复。

port 是一个**薄适配器**：

```
CubeMX 配好了硬件 ──→ port 提供运行时操作 ──→ app 编排业务逻辑
     (配置)               (启动/停止/读/写)         (算法/调度/UI)
```

port 的每一行代码都应该能回答这个问题：
**"这是在操作已经被 CubeMX 配好的外设的运行时行为吗？"**

如果答案是"否"，这行代码不属于 port。
