# OLED SPI DMA 双缓冲架构记录

## 背景

AC02 项目使用 SSD1306 128×64 OLED，4 线 SPI 接口（无 CS 引脚，硬件接地）。系统存在 10kHz 高频控制中断，OLED 刷新必须使用 DMA 避免 CPU 长时间阻塞。

## 最终架构

### 双缓冲

```
oled_buf_a[1024]  ←→  OLED_Buffer（绘制缓冲，ISR 写）
oled_buf_b[1024]  ←→  oled_send_buf（发送缓冲，DMA 读）
```

两个指针在 `OLED_SwapAndStart()` 中交换。ISR 始终渲染到 `OLED_Buffer`，DMA 始终从 `oled_send_buf` 读取，两者永远指向不同的物理缓冲。

### 状态机

```
空闲态（DMA 停止，画面静止）
  │
  │  20Hz ISR: oled_draw() 渲染 → OLED_SendBuffer() 设 new_frame=1
  │  主循环检测 IsNewFrame() && IsReady()
  ↓
  OLED_SwapAndStart(): 交换缓冲 → oled_start_dma() → 传输态

传输态（DMA 发送中）
  │
  │  20Hz ISR: 渲染 → 设 new_frame=1（不碰 DMA）
  │
  ├─ TC 回调: oled_dma_busy=0 → 空闲态
  │    主循环检测 IsNewFrame && IsReady → SwapAndStart → 传输态
  │
  └─ 无新帧: 画面静止，等待下一帧
```

### 中断优先级

| 优先级 | 中断 | 用途 |
|--------|------|------|
| 0 | TIM3, DMA1_Ch1/6/7 | 10kHz 控制环、ADC、UART |
| 1 | TIM7 | 200Hz 反馈环 |
| 6 | TIM6 | 20Hz OLED 渲染（只写缓冲+设标志） |
| 7 | DMA1_Ch3 | OLED SPI TC 回调（只标记空闲） |

OLED 相关中断全部排在 5 以后，不干扰控制环。

### 各上下文职责

| 上下文 | 能做什么 | 绝不做什么 |
|--------|---------|-----------|
| 20Hz ISR | 渲染到 OLED_Buffer、设 new_frame 标志 | 碰 SPI、碰 DMA、交换缓冲 |
| TC 回调 | 设 oled_dma_busy=0 | 交换缓冲、启动 DMA |
| 主循环 | 交换缓冲、启动 DMA | 渲染 |

## 踩过的坑

### 1. SPI DeInit/Init 产生毛刺（最终根因）

**现象：** 显示混乱、变暗、黑屏。

**原因：** 6 引脚 OLED 没有 CS 引脚，屏幕芯片一直在监听 SPI 总线。`oled_start_dma()` 中调用 `HAL_SPI_DeInit` + `HAL_SPI_Init` 会在 SCK/MOSI 上产生电平毛刺，SSD1306 把这些毛刺当成数据接收，导致内部寄存器被改写（对比度、显示开关等）。

**修复：** 删除 `HAL_SPI_DeInit` 和 `HAL_SPI_Init`。SPI 外设在 `MX_SPI1_Init()` 中初始化一次后不再复位。

### 2. panel_init 重复启动 DMA

**现象：** 上电即乱码。

**原因：** `OLED_Init()` 内部已调用 `oled_start_dma()` 发送首帧清屏。`panel_init()` 又调用 `OLED_SwapAndStart()` 启动第二次 DMA，此时上一帧 DMA 尚未完成，新的列/页命令和像素数据混入正在进行的传输，SSD1306 读乱。

**修复：** `panel_init()` 只调 `OLED_Init()` + `oled_draw()`。首帧 DMA 由 `OLED_Init` 启动，完成后 TC 回调标记空闲，主循环检测到新帧自动接管。

### 3. 缓冲交换被 ISR 打断

**现象：** 偶发错帧/乱码。

**原因：** 主循环交换 `OLED_Buffer` 和 `oled_send_buf` 指针时，TIM6 的 20Hz 中断打断交换过程，ISR 往交换了一半的 `OLED_Buffer` 渲染，导致渲染到正在被 DMA 读取的缓冲。

**修复：** `OLED_SwapAndStart()` 中用 `__disable_irq/__enable_irq` 保护指针交换。用 `__get_PRIMASK()` 保存当前中断状态，避免在已关中断的上下文中重复开关。

### 4. OLED_IsReady 只检查 HAL 状态

**现象：** `HAL_SPI_Transmit_DMA` 返回 `HAL_BUSY`，DMA 无法启动。

**原因：** 只检查 `hspi.State == READY` 不够。软件标志 `oled_dma_busy` 和 HAL 状态可能不同步（如 DMA 启动失败时 HAL 状态异常但软件标志仍为忙）。

**修复：** `OLED_IsReady()` 同时检查 `oled_dma_busy == 0` 和 `HAL_SPI_STATE_READY`。`oled_start_dma()` 中 `HAL_SPI_Transmit_DMA` 失败时复位 `oled_dma_busy`。

### 5. HAL 阻塞 SPI 与 DMA 混用

**原则：** `oled_start_dma()` 中先用阻塞 `HAL_SPI_Transmit` 发送 6 字节列/页地址命令（DC=LOW），再用 `HAL_SPI_Transmit_DMA` 发送 1024 字节像素数据（DC=HIGH）。两种方式共享同一个 SPI handle。

**注意事项：**
- 阻塞 transmit 完成后等待 `SPI_FLAG_BSY` 清零再切换 DC 引脚
- 不在阻塞命令之间调用 `DeInit/Init`（避免毛刺）
- DMA 启动失败时复位 `oled_dma_busy`，下一帧主循环重试

### 6. OLED_SendBuffer 在 ISR 中启动 DMA

**现象：** SPI 永久锁死在 `HAL_SPI_STATE_ERROR`。

**原因：** 20Hz ISR（优先级 6）在 `OLED_SendBuffer` 中判断 SPI 就绪后启动 DMA。10kHz 中断（优先级 0）在判断和启动之间抢占，TC 回调抢先检测到 SPI 空闲并启动 DMA 把 State 置为 BUSY。切回 20Hz 后它不知道状态已变，再次调用 `Transmit_DMA`，HAL 检测到 BUSY 进入 ERROR 状态。

**修复：** ISR 中 `OLED_SendBuffer()` 只设 `new_frame=1` 标志，绝不启动 DMA。DMA 启动只在主循环的 `OLED_SwapAndStart()` 中执行。

## 关键代码结构

```c
// oled_driver/oled.c

static uint8_t oled_buf_a[OLED_BUFFER_SIZE];
static uint8_t oled_buf_b[OLED_BUFFER_SIZE];
uint8_t *OLED_Buffer = oled_buf_a;
static uint8_t *oled_send_buf = oled_buf_b;
static volatile int oled_new_frame = 0;
static volatile int oled_dma_busy = 0;

static void oled_start_dma(void)
{
    OLED_DC_LOW();
    OLED_WriteCommand(CMD_SET_COL_ADDR);   // 阻塞发送 6 字节命令
    // ...
    while (__HAL_SPI_GET_FLAG(&hspi, SPI_FLAG_BSY)) {}
    OLED_DC_HIGH();

    if (hspi.State == HAL_SPI_STATE_READY) {
        oled_dma_busy = 1;
        if (HAL_SPI_Transmit_DMA(&hspi, oled_send_buf, SIZE) != HAL_OK) {
            oled_dma_busy = 0;
        }
    }
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    oled_dma_busy = 0;  // 只标记空闲
}

void OLED_SendBuffer(void)
{
    oled_new_frame = 1;  // 只设标志
}

void OLED_SwapAndStart(void)
{
    if (!OLED_IsReady()) return;
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    oled_new_frame = 0;
    uint8_t *tmp = OLED_Buffer;
    OLED_Buffer = oled_send_buf;
    oled_send_buf = tmp;
    if (!primask) __enable_irq();
    oled_start_dma();
}

int OLED_IsReady(void)
{
    return (oled_dma_busy == 0) && (hspi.State == HAL_SPI_STATE_READY);
}
```

```c
// app/app_panel.c

static void oled_draw(void) {
    OLED_Clear();
    // ... ShowStringEx ...
    OLED_SendBuffer();  // 只设标志
}

void app_on_20hz(void) {  // ISR，优先级 6
    key_scan();
    oled_draw();
    uart_send(status);
}

void panel_main_loop(void) {  // 主循环
    if (OLED_IsNewFrame() && OLED_IsReady()) {
        OLED_SwapAndStart();
    }
    // ... uart_read_line ...
}

void panel_init(void) {
    OLED_Init();   // 内部启动首帧 DMA
    oled_draw();   // 渲染第一帧到 draw buffer
}
```

## 设计原则总结

1. **ISR 只做快操作**：渲染内存 + 设标志，绝不碰外设
2. **DMA 启动单一入口**：只在主循环 `SwapAndStart` 中启动，消除竞态
3. **TC 回调最小化**：只标记空闲，不交换不启动
4. **指针交换原子化**：`__disable_irq` 保护，防 ISR 打断
5. **SPI 不复位**：无 CS 的 OLED 永远在监听，DeInit/Init 毛刺会被当成数据
6. **双重就绪检查**：软件标志 + HAL 状态，任一不满足则等待
