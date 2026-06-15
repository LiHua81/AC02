# DC-AC 全桥逆变器 开发提示词 — 算法篇

## 声明

本卷为**算法注入卷**，定义单相全桥 3 电平逆变器多速率控制系统的核心算法。**所有算法使用 float，不使用 Q24 定点数。** 算法层（`algo/`）零硬件依赖。

SOGI 系数为浮点值，硬编码在 `algo/sogi.c` 中。

---

## 第一章：多速率并发调度

系统两个控制速率：

| 速率 | 周期 | 触达 | 任务 |
|------|------|------|------|
| 10kHz | 100μs | TIM7 中断 | 相位累加 + SPWM 计算 + 更新 PWM 占空比 |
| 200Hz | 5ms | TIM6 中断 | ADC 采样、SOGI、Park、PI、参数生效 |

### 1.1 调制比 M 的双缓冲（影子寄存器）

200Hz 任务计算 M → 写入 `M_shadow`。
10kHz 任务在中断最开头：`M_active = M_shadow`，后续只用 `M_active`。

这确保 10kHz 任务的整个 100μs 周期内 M 值绝对一致，防止数据撕裂。

### 1.2 参数修改的双缓冲

UART SET 命令或按键 → 只写 `params_shadow{kp, ki, vref, freq}`，置 `pending = true`。
200Hz ISR 内统一调 `apply_params()`，原子替换到 `params_active`，同步更新 PI 增益和相位步长。

---

## 第二章：相位累加器（algo/phase_gen）

### 2.1 数据结构

```c
typedef struct {
    float phase_accum;     // 当前相位累加值，范围 0.0 ~ 200.0
    float freq_step;       // 每调用一步的增量
} phase_gen_t;
```

### 2.2 算法

**初始化**：`phase_accum = 0.0`，`freq_step = 1.0`（50Hz @ 10kHz：200 点/周期，每步 1 点）。

**累加**：
```
phase_accum += freq_step;
if (phase_accum >= 200.0f) phase_accum -= 200.0f;
```

**频率设置**：`freq_step = target_hz * 200.0f / 10000.0f`。例如 70Hz → step = 70*200/10000 = 1.4。

**正弦查表**：200 点 `sinf(2π * i / 200)` 表。`phase_sin()` → `float`（-1.0 ~ 1.0），`phase_cos()` → `float`（-1.0 ~ 1.0）。cos 通过相位偏移 50 点（90°）查同一张表。

**接口**：
```c
void phase_init(phase_gen_t *p);
void phase_advance(phase_gen_t *p);
void phase_set_freq(phase_gen_t *p, float hz);   // 40.0 ~ 70.0
float phase_sin(const phase_gen_t *p);
float phase_cos(const phase_gen_t *p);
```

---

## 第三章：200Hz 低频控制（SOGI → Park → PI）

### 3.1 SOGI 二阶广义积分器（algo/sogi）

**浮点系数（硬编码在 algo/sogi.c，禁止修改）：**

| 系数 | 值 |
|------|-----|
| `a1` | ` 1.93814003f` |
| `a2` | `-0.93909647f` |
| `bd0` | ` 0.03045174f` |
| `bd2` | `-0.03045174f` |
| `bq0` | ` 0.00047833f` |
| `bq1` | ` 0.00095667f` |
| `bq2` | ` 0.00047833f` |

**直通信号（α 轴）迭代公式：**
```
v_alpha = bd0 * x[i] + bd2 * x_old2 + a1 * va_old1 + a2 * va_old2
```

**正交信号（β 轴）迭代公式：**
```
v_beta = bq0 * x[i] + bq1 * x_old1 + bq2 * x_old2 + a1 * vb_old1 + a2 * vb_old2
```

**每次迭代末尾状态移位：**
```
x_old2 = x_old1
x_old1 = x[i]
va_old2 = va_old1
va_old1 = v_alpha
vb_old2 = vb_old1
vb_old1 = v_beta
```

**注意**：a1、a2 系数用正负已直接嵌入上式，实现时直接代入，不需要再变号。

**批量迭代**：输入 50 个 float 样本，依次执行上述迭代，状态持久化在 struct 中。输出 `vAlpha, vBeta` 为最后一次迭代的结果。

**接口：**
```c
typedef struct {
    float x_old1, x_old2;
    float va_old1, va_old2;
    float vb_old1, vb_old2;
} sogi_t;

void sogi_init(sogi_t *s);
void sogi_process_batch(sogi_t *s, const float *samples, int count,
                        float *vAlpha, float *vBeta);
```

### 3.2 Park 变换（algo/park）

`vD = vAlpha * cosθ + vBeta * sinθ`。单电压环，只输出 d 轴。

```c
float park_vd(float vAlpha, float vBeta, float cosT, float sinT);
```

### 3.3 PI 控制器 + 抗积分饱和（algo/pi）

```
error = ref - feedback
P = kp * error
I_temp = I_old + ki * error * Ts

Out_temp = P + I_temp

if (Out_temp > limit_max):
    Out = limit_max, I = limit_max - P
elif (Out_temp < limit_min):
    Out = limit_min, I = limit_min - P
else:
    Out = Out_temp, I = I_temp

I_old = I
return Out
```

**接口：**
```c
typedef struct {
    float kp, ki, ts;
    float limit_min, limit_max;
    float integrator;
} pi_t;

void pi_init(pi_t *p);
void pi_set_gains(pi_t *p, float kp, float ki, float ts_sec);
void pi_set_limits(pi_t *p, float min, float max);
float pi_update(pi_t *p, float ref, float feedback);
```

**默认值**：kp=0.2, ki=0.1, ts=0.005, limit_max=0.95, limit_min=-0.95。

---

## 第四章：SPWM 全桥 3 电平算法（algo/spwm）

### 4.1 算法（单极性 3 电平）

```
if (sinθ >= 0):
    // 正半周：左桥臂(CH1)调制，右桥臂(CH2)关断(下管导通)
    duty_a = clamp(mod * sinθ,      0.05, 0.95)
    duty_b = 0.05
else:
    // 负半周：左桥臂(CH1)关断(下管导通)，右桥臂(CH2)调制
    duty_a = 0.05
    duty_b = clamp(mod * (-sinθ),  0.05, 0.95)
```

**要点**：
- mod: 0.0 ~ 1.0（PI 输出经限幅的调制比）
- sinθ: -1.0 ~ 1.0（来自 phase_gen）
- 输出 duty_a, duty_b: 0.0 ~ 1.0，已含 5%/95% 死区限幅
- 全桥：CH1+CH1N 左桥臂，CH2+CH2N 右桥臂。硬件死区由 TIM8 BDTR 自动插入。

**接口：**
```c
void spwm_compute(float mod, float sin_theta, float *duty_a, float *duty_b);
```

---

## 第五章：200Hz 控制流程（在 app/control.c 实现）

```
每 5ms 执行一次：
  1. adc_get_batch(samples, 50)          // port/adc → 50 个 float V
  2. sogi_process_batch(sogi, samples, 50, &vAlpha, &vBeta)
  3. sin = phase_sin(phase), cos = phase_cos(phase)
  4. vD = park_vd(vAlpha, vBeta, cos, sin)
  5. mod = pi_update(pi, params_active.vref, vD)
  6. params_shadow.modulation = mod

  7. if (params_pending):
       apply_params()                    // 原子替换 kp/ki/vref/freq
       pi_set_gains(pi, params_active.kp, params_active.ki, 0.005f)
       phase_set_freq(phase, params_active.freq)
       params_pending = false
```

---

## 第六章：10kHz 控制流程（在 app/control.c 实现）

```
每 100μs 执行一次：
  1. mod_active = params_shadow.modulation    // 快照 M
  2. phase_advance(phase)
  3. sin = phase_sin(phase)
  4. spwm_compute(mod_active, sin, &duty_a, &duty_b)
  5. pwm_ch1_set(duty_a)                      // port/pwm
  6. pwm_ch2_set(duty_b)
```

---

## 第七章：算法校验标准

### 7.1 phase_gen
- 初值 sin=0, cos=1
- 200 次 advance 后 sin≈0, cos≈1（回零点）
- freq_set(70Hz) → step=1.4，200/1.4≈143 次 advance 回零点

### 7.2 spwm
- mod=0, sinθ=任意 → duty_a=duty_b=0.05
- mod=1, sinθ=1 → duty_a≈1.0, duty_b=0.05
- mod=0.5, sinθ=0.866 → duty_a≈0.433, duty_b=0.05

### 7.3 sogi
- 输入 50Hz 正弦，输出 vAlpha≈输入，vBeta 滞后 90°

### 7.4 pi
- kp=1, ki=0, ref=0.7, fb=0 → output=0.7
- 持续正误差 → 积分累加到上限后停止

### 7.5 park
- vAlpha=1, cosθ=1, vBeta=0 → vD=1
