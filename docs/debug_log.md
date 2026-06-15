# AC02 调试工作记录与四层架构反思

## 一、工作内容

### 1. Port 层修复

| 文件 | 问题 | 修复 |
|------|------|------|
| `port_pwm.c` | 硬编码 `PWM_ARR=799`，与 CubeMX 配置重复 | 改为 `__HAL_TIM_GET_AUTORELOAD(&htim8)` 从 handle 读取 |
| `port_timer.c` | `__HAL_TIM_CLEAR_FLAG` 配置类宏 | 删除，`HAL_TIM_Base_Start_IT` 内部已处理 |
| `port_key.c` | 去抖 5 次（250ms），响应过慢 | 改为 2 次（100ms），配合 20Hz 扫描 |
| `port_adc.c` | `adc_get_batch` 内部 `__disable_irq/__enable_irq` 破坏外层临界区 | 删除内部临界区，由调用方统一管理 |
| `port_uart.c` | 见下文"串口重构" | 完全重写 |

### 2. 串口重构（最大工作量）

**问题链：**

1. CubeMX 配置 DMA RX (circular) + DMA TX (normal)，但 port 层用 `HAL_UART_Receive_IT` (字节中断) 接收
2. `HAL_UART_Receive_IT` 把 `huart2.RxState` 设为 `BUSY_RX`
3. `HAL_UART_Transmit_DMA` 检查 `gState || RxState`，发现 `BUSY_RX` → 返回 `HAL_BUSY`
4. 所有后续发送全部被拒绝，程序表现如同"串口卡死"

**最终方案（标准 DMA 双通道 + IDLE）：**

```
TX: HAL_UART_Transmit_DMA (DMA1_Ch7, Normal)
    → TxCpltCallback → tx_busy = 0

RX: HAL_UART_Receive_DMA (DMA1_Ch6, Circular)
    → IDLE 中断 → uart_on_idle() → 行装配
    （不用 HalfCplt/Cplt，只靠 IDLE 检测"一句话说完"）
```

**关键教训：** HAL 的状态机假设 TX/RX 使用同一种方式。混搭（IT + DMA）必然冲突。要么全用 DMA，要么全用 IT。

### 3. 时钟与定时器修复

**根因：** 对 SYSCLK 频率的误判导致所有定时器 PSC/ARR 计算错误。

**修复后（72MHz SYSCLK, APB1=36MHz, APB2=72MHz, APB 定时器=72MHz）：**

| 定时器 | PSC | ARR | 频率 | 用途 | 优先级 |
|--------|-----|-----|------|------|--------|
| TIM3 | 71 | 99 | 10kHz | ADC 触发 + 控制环 ISR | 0 |
| TIM7 | 7199 | 49 | 200Hz | 反馈环 ISR | 1 |
| TIM6 | 7199 | 499 | 20Hz | UI 刷新 ISR | 6 |
| TIM8 | 0 | 3599 | 20kHz | SPWM（3600 级分辨率） | — |
| DMA1_Ch3 (SPI1 TX) | — | — | — | OLED 像素数据 | 7 |
| DMA1_Ch1 (ADC) | — | — | — | ADC 采样 | 0 |
| DMA1_Ch6/7 (UART) | — | — | — | UART RX/TX | 0 |

### 4. OLED 修复

**硬件修复：**
- `oled.h` 引脚从 GPIOA (PA4/PA6) 改为 GPIOB (PB4/PB6)，匹配 CubeMX 配置
- 删除 `OLED_GPIO_Init()`（重复定义 IO）
- SPI1 预分频 256→8（72MHz/8=9MHz），DMA 传输 1024 字节约 0.9ms
- CS 永久低电平（硬件接地）

**架构（双缓冲 + 状态机）：**

```
两个物理缓冲：oled_buf_a[1024], oled_buf_b[1024]
指针：OLED_Buffer → 绘制缓冲（ISR 写）
      oled_send_buf → 发送缓冲（DMA 读）

20Hz ISR (优先级 6):
  oled_draw()        → 渲染到 OLED_Buffer
  OLED_SendBuffer()  → 只设 oled_new_frame = 1，绝不碰 SPI/DMA

主循环 (线程模式):
  if (IsNewFrame && IsReady) → OLED_SwapAndStart():
    交换(OLED_Buffer ↔ oled_send_buf)
    oled_start_dma()  ← 唯一启动 DMA 的地方

TC 回调 (DMA1_Ch3, 优先级 7):
  oled_dma_busy = 0   ← 只标记空闲，不交换不启动
```

**未解决问题：OLED 显示不稳定（混乱、变暗、黑屏）**

已尝试的修复：
1. ✅ 双缓冲隔离 ISR 渲染和 DMA 读取
2. ✅ DMA 启动前 SPI DeInit+Init 复位外设
3. ✅ 阻塞命令后等待 BSY 标志再切 DC
4. ✅ 中断优先级分离（TIM6=6, DMA TC=7）
5. ✅ SPI 错误状态恢复
6. ❌ 仍然不稳定

**疑似根因：** `oled_start_dma()` 混用阻塞 SPI（命令）和 DMA（数据），两种 HAL 模式共享同一个 SPI handle 的状态机，阻塞 transmit 可能留下脏状态（RXNE/OVR 标志、移位寄存器残余）污染后续 DMA 传输，导致 SSD1306 收到错误命令（改变对比度/关闭显示）。

### 5. SOGI 崩溃修复

**现象：** `sogi_process_batch` 被 TIM3 的 10kHz ISR 抢占时崩溃。

**根因：** Cortex-M3 无 FPU，软件浮点库在 ISR 嵌套时的上下文保存问题。`adc_get_batch` 内部的 `__enable_irq()` 破坏了外层临界区保护。

**修复：**
- 删除 `adc_get_batch` 内部的 `__disable_irq/__enable_irq`
- 在 `app_on_200hz` 中用 `__disable_irq/__enable_irq` 包裹 SOGI 调用

### 6. 中断调度

**当前优先级分配（5 以后为低优先级 UI 类）：**

| 优先级 | 中断 | 用途 |
|--------|------|------|
| 0 | TIM3, DMA1_Ch1/6/7 | 10kHz 控制环、ADC、UART |
| 1 | TIM7 | 200Hz 反馈环 |
| 6 | TIM6 | 20Hz OLED 渲染（只写缓冲+设标志） |
| 7 | DMA1_Ch3 | OLED SPI TC 回调（只标记空闲） |

**OLED DMA 启动只发生在主循环（线程模式），ISR 绝不碰 SPI/DMA。**

---

## 三、最终中断传递图

```
硬件中断
  │
  ├─ TIM3_IRQHandler (优先级0, 10kHz)
  │    └─ HAL_TIM_IRQHandler
  │         └─ PeriodElapsedCallback
  │              └─ app_on_10khz()
  │                   ├─ phase_advance()
  │                   ├─ phase_sin()
  │                   ├─ spwm_compute()
  │                   └─ pwm_chX_set()  ← __HAL_TIM_SET_COMPARE
  │
  ├─ TIM7_IRQHandler (优先级1, 200Hz)
  │    └─ HAL_TIM_IRQHandler
  │         └─ PeriodElapsedCallback
  │              └─ app_on_200hz()
  │                   ├─ __disable_irq()
  │                   ├─ adc_get_batch()     ← 读 DMA 快照
  │                   ├─ sogi_process_batch() ← 50 样本 SOGI
  │                   ├─ __enable_irq()
  │                   ├─ phase_cos/sin()
  │                   ├─ park_vd()
  │                   ├─ pi_update()
  │                   ├─ params_shadow.vrms = |vD|/√2
  │                   └─ apply_params() (if pending)
  │
  ├─ TIM6_IRQHandler (优先级6, 20Hz)
  │    └─ HAL_TIM_IRQHandler
  │         └─ PeriodElapsedCallback
  │              └─ app_on_20hz()
  │                   ├─ key_scan()          ← HAL_GPIO_ReadPin
  │                   ├─ oled_draw()         ← 只写 OLED_Buffer
  │                   ├─ OLED_SendBuffer()   ← 只设 new_frame=1
  │                   └─ uart_send(status)   ← HAL_UART_Transmit_DMA
  │
  ├─ DMA1_Channel1_IRQHandler (优先级0, ADC DMA 完成)
  │    └─ HAL_DMA_IRQHandler
  │         └─ HAL_ADC_ConvCpltCallback
  │              └─ raw → float 快照，设 adc_snapshot_ready
  │
  ├─ DMA1_Channel3_IRQHandler (优先级7, OLED SPI TC)
  │    └─ HAL_DMA_IRQHandler
  │         └─ HAL_SPI_TxCpltCallback
  │              └─ oled_dma_busy = 0（只标记空闲）
  │
  ├─ DMA1_Channel6_IRQHandler (优先级0, UART RX DMA 完成)
  │    └─ HAL_DMA_IRQHandler (Circular 模式自动继续)
  │
  ├─ DMA1_Channel7_IRQHandler (优先级0, UART TX DMA 完成)
  │    └─ HAL_DMA_IRQHandler
  │         └─ HAL_UART_TxCpltCallback
  │              └─ tx_busy = 0
  │
  └─ USART2_IRQHandler (优先级3, IDLE/错误)
       ├─ IDLE 标志 → uart_on_idle() → 行装配
       └─ HAL_UART_IRQHandler → 错误处理 → ErrorCallback

主循环 (while 1)
  └─ panel_main_loop()
       ├─ if (IsNewFrame && IsReady) → SwapAndStart() ← 唯一启动 OLED DMA
       └─ uart_read_line() → parse_uart_command()
```

---

## 四、各层代码量

| 层 | 文件 | 行数 | 说明 |
|----|------|------|------|
| port/ | port_pwm.c | 41 | 从 handle 读 ARR |
| port/ | port_adc.c | 42 | DMA 快照，无内部临界区 |
| port/ | port_uart.c | 112 | DMA 双通道 + IDLE |
| port/ | port_timer.c | 12 | 只做 Start_IT |
| port/ | port_key.c | 34 | 2 次去抖 |
| algo/ | 5 个模块 | ~150 | 纯 float 数学 |
| app/ | app_control.c | 55 | 10kHz + 200Hz ISR 体 |
| app/ | app_params.c | 30 | 影子/生效参数 |
| app/ | app_panel.c | 195 | 20Hz ISR 体 + UART 命令 |
| Core/ | stm32f1xx_it.c | USER CODE ~25 | 回调分发 + USART2 IDLE |
| Core/ | main.c | USER CODE ~10 | 启动序列 |

port 层总计 ~241 行，控制在合理范围内。
