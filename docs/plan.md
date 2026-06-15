# DC-AC 全桥逆变器 四层总体架构

## 总览

```
app/   ── 编排层：控制循环、参数管理、UI 状态机
  │  调 algo/ 做计算，调 port/ 做输出
  │
algo/  ── 算法层：纯数学，零硬件依赖
  │  phase_gen → sogi → park → pi → spwm
  │  输入输出全是 float，不认识任何外设
  │
port/  ── 移植层：封装所有 STM32 HAL，换芯片只改这层
  │  pwm / adc / uart / timer / key
  │  暴露功能接口（set_duty），不暴露寄存器（CCR）
  │
Core/  ── CubeMX 生成，不动
  main.c 的 USER CODE 区只做两件事：调 runtime 启动 / 挂 ISR 回调
```

---

## 给 AI Coder 的提示词思路

### 核心原则

1. **CubeMX 的归 CubeMX，你的归你**
   - `MX_xxx_Init()` 已经全在 `main()` 里调过了，port/ 不做 init。
   - `Core/` 目录所有文件只动 `USER CODE` 区，其余代码是自动生成的。

2. **一层一层给，别一次全给**
   ```
   第一次: "实现 port/pwm.c，头文件已给出"
   第二次: "实现 algo/spwm.c，纯 float 数学"
   第三次: "实现 app/control.c，编排 port/ 和 algo/"
   ```
   一次只给一层的一个模块，AI 不会满天飞。

3. **每一层都有 docs/ 文档**
   - `docs/port.md` — port/ 各模块接口和用法
   - `docs/algo.md` — algo/ 算法接口和公式
   - `docs/app.md` — app/ 编排逻辑和数据流

4. **先给接口，再给实现**
   给 AI 看 `port/pwm.h` 的注释，再让它写 `port/pwm.c`。不要先聊需求再让它猜接口。

5. **算法先单测，再上板**
   `algo/` 零硬件依赖 → 用 gcc 在 PC 上跑单元测试，通过再集成。

### 三层分工口诀

```
port/ : "我知道芯片长什么样，不知道逆变器是什么"
algo/ : "我知道怎么算 SPWM，不知道芯片在哪"
app/  : "我知道怎么把算法和硬件串起来，但不认识寄存器名"
```

---

## 层与层之间的合同（接口头文件）

### port/ 暴露给 app/ 的接口

```
port/pwm.h
  pwm_start()                    — 使能全部 4 路（CH1/1N/2/2N），CubeMX 已配 AutoOutput 自动开 MOE
  pwm_stop()
  pwm_ch1_set(float duty)       — duty 0.0 ~ 1.0，ARR 从 handle 读
  pwm_ch2_set(float duty)

port/adc.h
  adc_start_dma()               — 启动 DMA 循环采样（main.c 调用）
  adc_read_v() → float          — 最新电压（V），port 内部做 raw→V 转换
  adc_get_batch(float *buf, n)  — 最近 n 个样本（SOGI 用 50 个）

port/uart.h
  uart_init()                   — 启用单字节中断接收（main.c 调用）
  uart_send(const char*)
  uart_read_line(char *buf)     — 非阻塞，收到\n返true

port/timer.h
  timer_start_isr()             — 启动 TIM3/6/7 中断
  // HAL_TIM_PeriodElapsedCallback 里直接 case→调 app/

port/key.h
  key_scan(void) → key_event_t  — 去抖后返回，20Hz 调一次
    事件: KEY_NONE / KEY0_SHORT / KEY1_SHORT
```

### algo/ 暴露给 app/ 的接口

```
algo/phase_gen.h
  phase_init(state)
  phase_advance(state)              — 每次 10kHz 调一次
  phase_set_freq(state, float hz)   — 40.0 ~ 70.0
  phase_sin(state) → float          — -1.0 ~ 1.0
  phase_cos(state) → float

algo/spwm.h
  spwm_compute(float mod, float sin_theta, float *duty_a, float *duty_b)
    — mod: 0.0 ~ 1.0 调制比
    — sin_theta: -1.0 ~ 1.0
    — duty_a, duty_b: 0.0 ~ 1.0（已含 5%/95% 死区限幅）
    — 内含单极性 3 电平全桥算法

algo/sogi.h
  sogi_init(state)
  sogi_process_batch(state, float samples[50], float *vAlpha, float *vBeta)
    — 50 样本批量迭代，状态持久化
    — 系数硬编码在 .c 里

algo/pi.h
  pi_init(state)
  pi_set_gains(state, float kp, float ki, float ts_s)
  pi_set_limits(state, float min, float max)
  pi_update(state, float ref, float feedback) → float
    — 内含抗积分饱和

algo/park.h
  park_vd(float vAlpha, float vBeta, float cosT, float sinT) → float
```

### app/ 调度逻辑

```
app/control.c
  10kHz ISR 回调:
    1. modulation_active = params_shadow.modulation
    2. phase_advance()
    3. spwm_compute(mod, sin) → duty_a, duty_b
    4. pwm_ch1_set(duty_a), pwm_ch2_set(duty_b)

  200Hz ISR 回调:
    1. adc_get_batch(buf, 50)
    2. sogi_process_batch(buf) → vAlpha, vBeta
    3. park_vd(vAlpha, vBeta, cos, sin) → vD
    4. pi_update(vref, vD) → modulation → 写影子寄存器

app/params.c
  影子寄存器: params_shadow (kp/ki/vref/freq)
  生效寄存器: params_active
  apply_params(): 原子替换 + phase_set_freq()

app/panel.c
  20Hz ISR / 主循环:
    - key_scan() → KEY0: vref+step, KEY1: vref-step
    - UART 行解析 → SET P:/I:/V:/F: → 写影子寄存器
    - OLED 刷新: Vref / Vrms / Mod / Freq
```

---

## 关键隔离检查规则

```
┌──────────┬──────────────────────────────────────────────────────────────┐
│ 目录     │ 隔离规则                                                     │
├──────────┼──────────────────────────────────────────────────────────────┤
│ algo/    │ 禁止: TIM, CCR, HAL, __HAL, GPIO, NVIC, DMA, ADC, USART   │
│          │ 允许: float, int, 数学运算, 标准库 <string.h> <math.h>     │
├──────────┼──────────────────────────────────────────────────────────────┤
│ port/    │ 允许(业务):  HAL_TIM_PWM_Start/Stop, HAL_ADC_Start_DMA,   │
│          │             HAL_TIM_Base_Start_IT, HAL_GPIO_ReadPin,       │
│          │             HAL_UART_Receive_IT, HAL_UART_Transmit_DMA,    │
│          │             __HAL_TIM_SET_COMPARE, __HAL_TIM_GET_AUTORELOAD│
│          │             HAL weak 回调覆写                               │
│          │ 禁止(配置):  HAL_xxx_Init, MX_xxx_Init, HAL_xxx_Config*,  │
│          │             HAL_xxx_MspInit, HAL_GPIO_Init, HAL_NVIC_*,   │
│          │             HAL_DMA_Init, __HAL_LINKDMA,                   │
│          │             __HAL_TIM_SET_PRESCALER/AUTORELOAD,            │
│          │             __HAL_TIM_CLEAR_FLAG, __HAL_UART_ENABLE_IT,   │
│          │             TIMx->, GPIOx->, ADCx-> 直接寄存器访问         │
│          │ 禁止(算法):  sogi, pi, spwm, park, phase_gen              │
│          │ 禁止(硬编码): ARR/PSC/波特率等 CubeMX 已配参数值           │
├──────────┼──────────────────────────────────────────────────────────────┤
│ app/     │ 允许: #include "algo/xxx.h", #include "port/xxx.h"         │
│          │ 禁止: HAL_TIM(直接), CCR, __HAL_TIM_SET_COMPARE(直接),   │
│          │         GPIO_TypeDef, TIM_HandleTypeDef 等 HAL 类型        │
│          │ 例外: HAL_Delay() — 后续可替换为 port 提供的延时接口       │
├──────────┼──────────────────────────────────────────────────────────────┤
│ Core/    │ 只碰 USER CODE 区:                                         │
│          │   main.c BEGIN 2: 调 port/ 启动函数 + app/ 初始化          │
│          │   it.c BEGIN 1: HAL_TIM_PeriodElapsedCallback 转发到 app/ │
│          │ 禁止: USER CODE 以外的任何修改                              │
└──────────┴──────────────────────────────────────────────────────────────┘
```

---

## 数据流（一图）

```
ADC → port/adc → app/control(200Hz) → algo/sogi → algo/park → algo/pi
                                                            ↓
                                                    params_shadow.modulation
                                                            ↓
                                              app/control(10kHz) → algo/spwm
                                                            ↓
                                                    duty_a, duty_b (0~1.0)
                                                            ↓
                                                      port/pwm → TIM8 → 桥臂

UART → port/uart → app/panel(SET cmd) → params_shadow.{kp,ki,vref,freq}
                                              ↓
                                      params 生效 → algo/pi(新增益), algo/phase_gen(新频率)
```

---

## 各层文件细化

### port/ — 移植层（详细规范）

#### 核心思想

port/ 是 CubeMX 生成代码与业务代码之间的**运行时桥梁**。它唯一的工作是：用 HAL 库提供的**业务函数**，操作已经被 CubeMX 完整配置好的外设。

**分工口诀**：
```
CubeMX : "我把外设配好了，时钟、GPIO、NVIC、DMA 全搞定了"
port/  : "我不改任何配置，我只按'启动/停止/读/写'按钮"
algo/  : "我不知道有外设，我只做数学"
app/   : "我把 port 和 algo 串起来，不认识寄存器"
```

#### port/ 可以做什么

| 操作类型 | HAL 示例 | 说明 |
|---------|---------|------|
| 运行时启动/停止 | `HAL_TIM_PWM_Start()`, `HAL_ADC_Start_DMA()`, `HAL_TIM_Base_Start_IT()`, `HAL_UART_Receive_IT()` | 使能已经配好的外设 |
| 运行时写入 | `__HAL_TIM_SET_COMPARE()` | 更新 PWM 占空比（运行时数据，非配置参数） |
| 运行时读取 | `HAL_GPIO_ReadPin()`, `__HAL_TIM_GET_AUTORELOAD()` | 读 GPIO 电平、从 handle 读 ARR 等参数 |
| 覆写弱回调 | `HAL_ADC_ConvCpltCallback()`, `HAL_UART_RxCpltCallback()`, `HAL_UART_TxCpltCallback()`, `HAL_UART_ErrorCallback()` | 处理 HAL 中断完成事件 |

#### port/ 禁止做什么

| 禁止类型 | 举例 | 为什么禁止 |
|---------|------|----------|
| 调用配置函数 | `HAL_TIM_PWM_ConfigChannel()`, `HAL_TIM_Base_Init()`, `HAL_ADC_ConfigChannel()`, `HAL_UART_Init()`, `HAL_GPIO_Init()` | CubeMX 的 `MX_xxx_Init()` 已配完 |
| 调用 MX 函数 | `MX_TIM8_Init()`, `MX_ADC1_Init()` | 只在 `main()` 里调一次 |
| 配置类 HAL 宏 | `__HAL_TIM_SET_PRESCALER()`, `__HAL_TIM_SET_AUTORELOAD()`, `__HAL_UART_ENABLE_IT()` | 这是修改外设配置，不是运行时操作 |
| 直接寄存器操作 | `TIM8->CCRx = …`, `GPIOA->ODR = …`, `ADC1->CR2 |= …` | 绕过 HAL 抽象层 |
| 硬编码 CubeMX 已配参数 | `#define PWM_ARR 799`, `#define ADC_RESOLUTION 4095` | CubeMX 改了你就得跟着改，容易遗漏 |
| 配置 NVIC | `HAL_NVIC_SetPriority()`, `HAL_NVIC_EnableIRQ()` | CubeMX 的 MspInit 已配完 |
| 初始化 DMA | `HAL_DMA_Init()`, `__HAL_LINKDMA()` | CubeMX 的 MspInit 已配完 |

#### 关键原则："从 handle 读，不硬编码"

port/ 需要的所有硬件参数，都从 CubeMX 生成的 handle 里读。这保证 CubeMX 配置变更时 port/ 零修改。

```c
// ✗ 错误 — 硬编码了 CubeMX 里设的 ARR 值
#define PWM_ARR 799
uint32_t ccr = (uint32_t)(duty * PWM_ARR);

// ✓ 正确 — 从 handle 读 ARR，CubeMX 改了也不影响
uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim8);
uint32_t ccr = (uint32_t)(duty * arr);
```

```c
// ✗ 错误 — 硬编码 ADC 分辨率
#define ADC_MAX_VALUE 4095.0f

// ✓ 正确 — 12-bit 右对齐是 F1 系列的固定特性，用常量表达语义
// ADC 12-bit 右对齐 → 最大值 4095，这是芯片固有属性，非 CubeMX 配置
static inline float raw_to_volt(uint16_t raw) {
    return ((float)raw) * (3.3f / 4095.0f);
}
```

```c
// ✗ 错误 — 启动前手动清标志（CubeMX + HAL_TIM_Base_Start_IT 已处理）
__HAL_TIM_CLEAR_FLAG(&htim3, TIM_FLAG_UPDATE);

// ✓ 正确 — 直接启动，HAL 内部会处理
HAL_TIM_Base_Start_IT(&htim3);
```

#### 设计原则："少即是多"

每个 port 模块应该**尽可能短小**。port 不是业务层，不应该有：
- 冗余的错误处理（HAL 已处理）
- 多余的状态管理（除非业务需要）
- 额外的初始化逻辑（CubeMX 已初始化）
- 对硬件参数的假设（从 handle 读）

如果一个 port 模块超过 80 行，大概率做了不该做的事。

---

#### 各模块详细规范

##### port/pwm.c — TIM8 全桥 PWM 运行时

**职责**：启动/停止 PWM 输出，运行时更新占空比。

| 项目 | 内容 |
|------|------|
| 生命周期 | `pwm_start()` → 4 路使能；`pwm_stop()` → 4 路停止 |
| 运行时 | `pwm_chX_set(float)` → duty(0~1) 映射到 CCR |
| **允许使用** | `HAL_TIM_PWM_Start/Stop`, `HAL_TIMEx_PWMN_Start/Stop`, `__HAL_TIM_SET_COMPARE`, `__HAL_TIM_GET_AUTORELOAD` |
| **禁止使用** | `HAL_TIM_PWM_ConfigChannel`, `HAL_TIM_Base_Init`, `HAL_TIMEx_ConfigBreakDeadTime`, `__HAL_TIM_SET_PRESCALER`, `__HAL_TIM_SET_AUTORELOAD` |
| 参数来源 | ARR 从 `htim8.Init.Period` 或 `__HAL_TIM_GET_AUTORELOAD(&htim8)` 读取，**不硬编码** |
| 对上层隐藏 | CCxE/CCxNE/MOE 位操作、CCR 寄存器地址、ARR 值、死区时间、极性配置 |

```c
// port/pwm.c 应该只有这些：
extern TIM_HandleTypeDef htim8;

void pwm_start(void) {
    HAL_TIM_PWM_Start(&htim8, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim8, TIM_CHANNEL_2);
    HAL_TIMEx_PWMN_Start(&htim8, TIM_CHANNEL_1);
    HAL_TIMEx_PWMN_Start(&htim8, TIM_CHANNEL_2);
}

void pwm_stop(void) { /* 对称 Stop */ }

void pwm_ch1_set(float duty) {
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim8);
    duty = clamp(duty, 0.05f, 0.95f);
    __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_1, (uint32_t)(duty * arr));
}
```

##### port/adc.c — ADC1 DMA 采样

**职责**：启动 DMA 循环采样，DMA 完成回调中做 raw→V 转换，提供批量数据访问。

| 项目 | 内容 |
|------|------|
| 生命周期 | `adc_start_dma()` → 启动 DMA 循环模式 |
| 运行时 | `HAL_ADC_ConvCpltCallback()` 覆写 → raw→float 快照；`adc_get_batch()` 取数据；`adc_read_v()` 取最新值 |
| **允许使用** | `HAL_ADC_Start_DMA`, `HAL_ADC_Stop_DMA`, `HAL_ADC_ConvCpltCallback`(weak覆写) |
| **禁止使用** | `HAL_ADC_Init`, `HAL_ADC_ConfigChannel`, `HAL_ADC_MspInit`, `HAL_DMA_Init`, ADC 寄存器直接操作 |
| 参数来源 | 批量大小从 DMA buffer 大小决定（50 样本 = 一个 TIM3_TRGO 触发周期），3.3V/12-bit 是芯片固有属性 |
| 对上层隐藏 | raw ADC 值、DMA buffer 地址、DMA 完成中断、raw→V 转换公式 |

```c
// port/adc.c 关键结构：
static uint16_t adc_dma_buf[50];        // DMA 目标 buffer
static float    adc_snapshot[50];        // raw→V 后的快照
static volatile int adc_snapshot_ready;  // 快照就绪标志

void adc_start_dma(void) {
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_dma_buf, 50);
}

// DMA 完成回调（HAL weak override）
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc) {
    if (hadc->Instance == ADC1) {
        for (int i = 0; i < 50; i++)
            adc_snapshot[i] = (float)adc_dma_buf[i] * (3.3f / 4095.0f);
        adc_snapshot_ready = 1;
    }
}

void adc_get_batch(float *buf, int n) {
    // 关中断 → memcpy snapshot → 开中断
}
```

##### port/uart.c — USART2 收发

**职责**：运行时收发。RX 用字节中断逐字拼行，TX 用 DMA NORMAL 发送。

| 项目 | 内容 |
|------|------|
| 生命周期 | `uart_init()` → `HAL_UART_Receive_IT()` 启用单字节中断接收 |
| 运行时 | `uart_send()` → DMA NORMAL 发送；`uart_read_line()` → 非阻塞读一行 |
| **允许使用** | `HAL_UART_Receive_IT`, `HAL_UART_Transmit_DMA`, `HAL_UART_RxCpltCallback`(weak), `HAL_UART_TxCpltCallback`(weak), `HAL_UART_ErrorCallback`(weak) |
| **禁止使用** | `HAL_UART_Init`, `HAL_UART_MspInit`, `HAL_DMA_Init`, `__HAL_UART_ENABLE_IT`, 直接操作 USART 寄存器 |
| 回调说明 | RxCplt: 处理收到的字节 + 重新 arm 单字节中断；TxCplt: 清 tx_busy；ErrorCallback: 恢复 RX + 清 tx_busy |
| 对上层隐藏 | 中断向量、DMA 通道、tx_busy 状态、rx_byte 变量、行拼接逻辑 |

```c
// port/uart.c 关键结构：
static uint8_t  rx_byte;                    // 单字节接收目标
static char     rx_buffer[128];             // 行缓冲
static volatile int rx_head, rx_line_ready; // 缓冲状态
static char     tx_buffer[128];             // 发送缓冲（DMA 需要稳定地址）
static volatile int tx_busy;

void uart_init(void) {
    HAL_UART_Receive_IT(&huart2, &rx_byte, 1);  // arm 第一次字节中断
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART2) {
        uart_process_byte(rx_byte);              // 拼行
        HAL_UART_Receive_IT(&huart2, &rx_byte, 1); // 重新 arm
    }
}
```

##### port/timer.c — TIM3/6/7 中断启动

**职责**：启动三个定时器的中断。仅此而已。

| 项目 | 内容 |
|------|------|
| 生命周期 | `timer_start_isr()` → 启动 TIM3/6/7 中断 |
| **允许使用** | `HAL_TIM_Base_Start_IT` |
| **禁止使用** | `HAL_TIM_Base_Init`, `HAL_TIM_ConfigClockSource`, `__HAL_TIM_CLEAR_FLAG`(启动前清标志 — HAL 内部已处理), `HAL_NVIC_SetPriority`, `HAL_NVIC_EnableIRQ` |
| 回调位置 | `HAL_TIM_PeriodElapsedCallback` 放在 `Core/Src/stm32f1xx_it.c` 的 USER CODE 区，**不在 port/timer.c 里实现** |
| 对上层隐藏 | TIM 句柄实例、NVIC 优先级号 |

```c
// port/timer.c 应该只有这么短：
extern TIM_HandleTypeDef htim3, htim6, htim7;

void timer_start_isr(void) {
    HAL_TIM_Base_Start_IT(&htim3);
    HAL_TIM_Base_Start_IT(&htim6);
    HAL_TIM_Base_Start_IT(&htim7);
}
```

##### port/key.c — 按键读取

**职责**：读 GPIO + 软件去抖。最简单的 port 模块。

| 项目 | 内容 |
|------|------|
| 运行时 | `key_scan()` → 读引脚电平，连续 N 次一致才确认 |
| **允许使用** | `HAL_GPIO_ReadPin` |
| **禁止使用** | `HAL_GPIO_Init`, `HAL_GPIO_WritePin`(按键不需要写), EXTI 相关 |
| 对上层隐藏 | GPIO 端口/引脚号、有效电平（低/高）、去抖次数阈值 |

```c
// port/key.c — GPIO 引脚和去抖参数是 port 层内部细节
static key_state_t keys[] = {
    {GPIOC, GPIO_PIN_5, 0, KEY0_SHORT},
    {GPIOA, GPIO_PIN_15, 0, KEY1_SHORT}
};

key_event_t key_scan(void) {
    // HAL_GPIO_ReadPin + 去抖计数
}
```

---

#### CubeMX 配置 → port 使用 参数契约表

此表确保 port/ 和 CubeMX 配置同步。**port/ 不假设任何数值，从 handle 读或按 CubeMX 配置使用。**

| CubeMX 配置项（在 Core/Src/ 中） | 配置值 | port/ 如何使用 |
|--------------------------------|-------|--------------|
| TIM8 PSC=0, ARR=799 | ~90kHz PWM | port/pwm: 从 `__HAL_TIM_GET_AUTORELOAD(&htim8)` 读 ARR |
| TIM8 OC polarity=LOW, deadtime=10 | 互补全桥 | port/pwm: 不做任何配置，直接 Start |
| TIM8 AutoOutput=ENABLE | MOE 自动使能 | port/pwm: 不需要手动设 MOE |
| TIM7 PSC=79, ARR=99 | 调度 10kHz | port/timer: `HAL_TIM_Base_Start_IT(&htim7)` 即可 |
| TIM6 PSC=799, ARR=999 | 调度 200Hz | port/timer: `HAL_TIM_Base_Start_IT(&htim6)` 即可 |
| TIM3 PSC=79, ARR=199 | 调度 20Hz + ADC TRGO | port/timer: `HAL_TIM_Base_Start_IT(&htim3)` 即可 |
| ADC1 ext trigger=TIM3_TRGO | 定时采样 | port/adc: `HAL_ADC_Start_DMA` 即可，触发自动进行 |
| ADC1 DMA circular, halfword | 循环搬运 | port/adc: buf 大小 50，匹配一个触发周期的采样数 |
| USART2 115200 8N1 | 串口通信 | port/uart: 不碰波特率，直接 `Receive_IT` / `Transmit_DMA` |
| USART2 DMA RX circular | CubeMX 配了但 port 不用 | port/uart: 选择 `Receive_IT` 单字节中断（更适合命令行逐字拼行） |
| USART2 DMA TX normal | CubeMX 已配 | port/uart: 用 `Transmit_DMA` 发送 |
| GPIO PC5/PA15 输入上拉 | 按键 | port/key: `HAL_GPIO_ReadPin` 读电平，按下为 LOW |

#### port/ 自检清单（写完每个模块后逐条检查）

- [ ] 没有任何 `MX_xxx_Init()` 调用
- [ ] 没有任何 `HAL_xxx_Init()` 调用
- [ ] 没有任何 `HAL_xxx_MspInit()` / `HAL_xxx_MspDeInit()` 调用
- [ ] 没有任何 `HAL_xxx_Config*()` 调用（ConfigChannel, ConfigClockSource 等）
- [ ] 没有硬编码 CubeMX 已配的参数值（ARR、PSC、波特率等）
- [ ] 没有 `__HAL_xxx_SET_*` 配置类宏（SET_PRESCALER, SET_AUTORELOAD 等）
- [ ] 没有直接寄存器访问（`TIMx->`, `GPIOx->`, `ADCx->`, `USARTx->`, `DMAx->`）
- [ ] 没有 `HAL_GPIO_Init` 调用
- [ ] 没有 `HAL_NVIC_*` 调用
- [ ] 没有 `HAL_DMA_Init` / `__HAL_LINKDMA` 调用
- [ ] 模块代码量 < 80 行（不含回调）
- [ ] 没有 `#include "algo/xxx.h"` 或 `#include "app/xxx.h"`

**文档**: `docs/port.md` — 每个模块接口 + 调用示例

### algo/ — 算法层

纯 C，输入输出 float，零硬件依赖。可 PC 单测。每个模块 `.c` + `.h`。

| 文件 | 职责 | 核心输入→输出 | 关键细节 |
|------|------|-------------|---------|
| `algo/phase_gen.h/.c` | 相位累加器 | `advance()` 每调用一步, `sin()` `cos()` 返回 float | 200 点表，`freq_step = target_hz / call_rate_hz`，`set_freq()` 支持 40-70Hz |
| `algo/spwm.h/.c` | 全桥 3 电平 SPWM | `mod(0~1) + sinθ(-1~1)` → `duty_a, duty_b(0~1)` | 正半周 CH1 调制 CH2 关断，负半周反之；内含 5%/95% 限幅 |
| `algo/sogi.h/.c` | 二阶广义积分器 | `samples[50]` → `vAlpha, vBeta` | 系数硬编码（a1/a2/bd0/bd2/bq0/bq1/bq2），状态持久化在 struct 里 |
| `algo/pi.h/.c` | PI 控制器 | `ref, feedback` → `output` | `set_gains(kp, ki, ts_sec)` ts 单位秒；内含抗积分饱和 |
| `algo/park.h/.c` | Park 变换 | `vAlpha, vBeta, cosT, sinT` → `vD` | 只输出 d 轴，单电压环屏蔽 q 轴 |

**文档**: `docs/algo.md` — 每个算法公式 + 参数范围 + 单测方法

### app/ — 编排层

调用 algo/ 做计算，调用 port/ 做输入输出。每个模块 `.c` + `.h`。

| 文件 | 职责 | 伪代码流 |
|------|------|---------|
| `app/control.h/.c` | ISR 级控制循环 | 10k: `phase_advance → spwm_compute → pwm_chX_set`；200Hz: `adc_get_batch → sogi → park → pi_update → 写影子` |
| `app/params.h/.c` | 参数管理 | `params_shadow{kp,ki,vref,freq}`，`apply_params()` 原子替换到 `params_active` + 调 `phase_set_freq/pi_set_gains` |
| `app/panel.h/.c` | 人机界面 | 主循环：`key_scan()` 映射到 vref±step；`uart_read_line()` 解析 SET→写影子；OLED 显示 Vref/Vrms/Mod/Freq；`uart_send()` 定时上报状态 |

**关键规则**:
- `params_shadow` 只由 panel 写入（按键/UART），只在 200Hz ISR 里 `apply_params()` 统一生效
- `app/control.c` 不调 `pi_set_gains/phase_set_freq`，这些由 `apply_params()` 统一处理
- OLED 和 UART 上报放主循环，ISR 只置标志位

**文档**: `docs/app.md` — 数据流、ISR 调度时序、参数生效机制

### Core/ — 只碰 USER CODE

| 位置 | 写入内容 | 禁止 |
|------|---------|------|
| `main.c` USER CODE 2 | `pwm_start(); adc_start_dma(); timer_start_isr();` | 在此写算法逻辑、寄存器操作 |
| `stm32f1xx_it.c` USER CODE | `HAL_TIM_PeriodElapsedCallback` 里 case TIM7→`app_on_10khz()` 等 | 在此写业务逻辑 |
| `CMakeLists.txt` | 加 `port/ algo/ app/` 三个目录的源文件 | 改全局编译选项 |
