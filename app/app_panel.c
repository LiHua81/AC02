#include "app_panel.h"
#include "app_params.h"
#include "port_key.h"
#include "port_uart.h"
#include "oled_driver/oled.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define VREF_STEP 0.5f
#define V_SCALE   20.0f    /* modulation 1.0 → 20Vrms */

static char uart_buf[128];
static char oled_buf[64];
static char status_buf[128];

static int f2i2(float v) {
    int neg = (v < 0);
    if (neg) v = -v;
    int r = (int)(v * 100.0f + 0.5f);
    return neg ? -r : r;
}

static int f2i1(float v) {
    int neg = (v < 0);
    if (neg) v = -v;
    int r = (int)(v * 10.0f + 0.5f);
    return neg ? -r : r;
}

static int parse_float(const char *s, float *out) {
    int i = 0;
    int neg = 0;
    if (s[i] == '-') { neg = 1; i++; }
    else if (s[i] == '+') { i++; }

    int int_part = 0;
    while (s[i] >= '0' && s[i] <= '9') {
        int_part = int_part * 10 + (s[i] - '0');
        i++;
    }

    float frac = 0, div = 1;
    if (s[i] == '.') {
        i++;
        while (s[i] >= '0' && s[i] <= '9') {
            frac = frac * 10 + (s[i] - '0');
            div *= 10;
            i++;
        }
    }

    if (i == 0 || (i == 1 && neg)) return 0;

    *out = (float)int_part + frac / div;
    if (neg) *out = -(*out);
    return i;
}

static const char *skip_prefix(const char *line, const char *prefix) {
    while (*prefix) {
        if (*line != *prefix) return NULL;
        line++; prefix++;
    }
    return line;
}

static void oled_draw(void) {
    OLED_Clear();

    float vrms = params_shadow.vrms;
    int v = f2i1(vrms);
    int vr = f2i1(params_active.vref);
    sprintf(oled_buf, "Vr:%d.%d Vf:%d.%d", v / 10, abs(v) % 10, vr / 10, abs(vr) % 10);
    OLED_ShowStringEx(0, 0, oled_buf, FONT_LARGE);

    int fr = f2i1(params_active.freq);
    int md = f2i2(params_shadow.modulation);
    sprintf(oled_buf, "Freq:%d.%dHz Mod:%d.%02d", fr / 10, abs(fr) % 10, md / 100, abs(md) % 100);
    OLED_ShowStringEx(0, 20, oled_buf, FONT_MEDIUM);

    int kp = f2i2(params_active.kp);
    int ki = f2i2(params_active.ki);
    sprintf(oled_buf, "P:%d.%02d I:%d.%02d", kp / 100, abs(kp) % 100, ki / 100, abs(ki) % 100);
    OLED_ShowStringEx(0, 34, oled_buf, FONT_MEDIUM);

    OLED_SendBuffer();  /* 只设 new_frame 标志 */
}

void panel_init(void) {
    OLED_Init();
    oled_draw();
}

static void parse_uart_command(const char *line) {
    float val;
    const char *p;

    if ((p = skip_prefix(line, "SET P:")) && parse_float(p, &val)) {
        params_shadow.kp = val;
        params_pending = 1;
        int v = f2i2(val);
        sprintf(status_buf, "[ECHO] KP set to %d.%02d\r\n", v / 100, abs(v) % 100);
        uart_send(status_buf);
    } else if ((p = skip_prefix(line, "SET I:")) && parse_float(p, &val)) {
        params_shadow.ki = val;
        params_pending = 1;
        int v = f2i2(val);
        sprintf(status_buf, "[ECHO] KI set to %d.%02d\r\n", v / 100, abs(v) % 100);
        uart_send(status_buf);
    } else if ((p = skip_prefix(line, "SET V:")) && parse_float(p, &val)) {
        if (val >= 0.5f && val <= 20.0f) {
            params_shadow.vref = val;
            params_pending = 1;
            int v = f2i1(val);
            sprintf(status_buf, "[ECHO] Vref set to %d.%dV\r\n", v / 10, abs(v) % 10);
            uart_send(status_buf);
        }
    } else if ((p = skip_prefix(line, "SET F:")) && parse_float(p, &val)) {
        if (val >= 40.0f && val <= 70.0f) {
            params_shadow.freq = val;
            params_pending = 1;
            int v = f2i1(val);
            sprintf(status_buf, "[ECHO] Freq set to %d.%d Hz\r\n", v / 10, abs(v) % 10);
            uart_send(status_buf);
        }
    } else if (strncmp(line, "STATUS", 6) == 0) {
        int vrms = f2i1(params_shadow.vrms);
        int vr = f2i1(params_active.vref);
        int md = f2i2(params_shadow.modulation);
        int fr = f2i1(params_active.freq);
        sprintf(status_buf,
            "[STATUS] Vrms=%d.%dV Vref=%d.%dV Mod=%d.%02d Freq=%d.%dHz\r\n",
            vrms / 10, abs(vrms) % 10,
            vr / 10, abs(vr) % 10,
            md / 100, abs(md) % 100,
            fr / 10, abs(fr) % 10);
        uart_send(status_buf);
    } else if (strncmp(line, "PARAMS", 6) == 0) {
        int kp = f2i2(params_active.kp);
        int ki = f2i2(params_active.ki);
        int vr = f2i1(params_active.vref);
        int fr = f2i1(params_active.freq);
        sprintf(status_buf,
            "[PARAMS] KP=%d.%02d KI=%d.%02d Vref=%d.%dV Freq=%d.%dHz\r\n",
            kp / 100, abs(kp) % 100,
            ki / 100, abs(ki) % 100,
            vr / 10, abs(vr) % 10,
            fr / 10, abs(fr) % 10);
        uart_send(status_buf);
    }
}

void app_on_20hz(void) {
    key_event_t key = key_scan();
    if (key == KEY0_SHORT) {
        params_shadow.vref += VREF_STEP;
        if (params_shadow.vref > 20.0f) params_shadow.vref = 20.0f;
        params_pending = 1;
        int v = f2i1(params_shadow.vref);
        sprintf(status_buf, "[ECHO] KEY0: Vref up to %d.%dV\r\n", v / 10, abs(v) % 10);
        uart_send(status_buf);
    } else if (key == KEY1_SHORT) {
        params_shadow.vref -= VREF_STEP;
        if (params_shadow.vref < 0.5f) params_shadow.vref = 0.5f;
        params_pending = 1;
        int v = f2i1(params_shadow.vref);
        sprintf(status_buf, "[ECHO] KEY1: Vref down to %d.%dV\r\n", v / 10, abs(v) % 10);
        uart_send(status_buf);
    }

    oled_draw();

    int vrms = f2i1(params_shadow.vrms);
    int vr = f2i1(params_active.vref);
    int md = f2i2(params_shadow.modulation);
    int fr = f2i1(params_active.freq);
    int kp = f2i2(params_active.kp);
    int ki = f2i2(params_active.ki);
    sprintf(status_buf,
        "Vrms=%d.%dV Vref=%d.%dV F=%d.%dHz M=%d.%02d P=%d.%02d I=%d.%02d\r\n",
        vrms / 10, abs(vrms) % 10,
        vr / 10, abs(vr) % 10,
        fr / 10, abs(fr) % 10,
        md / 100, abs(md) % 100,
        kp / 100, abs(kp) % 100,
        ki / 100, abs(ki) % 100);
    uart_send(status_buf);
}

void panel_main_loop(void) {
    if (uart_read_line(uart_buf)) {
        parse_uart_command(uart_buf);
    }

    /* 主循环是唯一启动 DMA 的地方（初始化除外）
     * ISR 只渲染 + 设标志，绝不碰 SPI/DMA */
    if (OLED_IsNewFrame() && OLED_IsReady()) {
        OLED_SwapAndStart();
    }
}
