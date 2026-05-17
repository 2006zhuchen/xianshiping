#include "Fpga.h"
#include <string.h>

/* ==================================================================
 *  第一部分：GPIO 波形控制（PB5/PB6/PB7）—— 不变
 * ================================================================== */

void FPGA_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    FPGA_SetWave(FPGA_WAVE_SINE);
}

void FPGA_SetWave(FPGA_WaveType wave)
{
    GPIO_ResetBits(GPIOB, GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7);

    switch (wave)
    {
        case FPGA_WAVE_SINE:
            GPIO_SetBits(GPIOB, GPIO_Pin_5);
            break;
        case FPGA_WAVE_TRI:
            GPIO_SetBits(GPIOB, GPIO_Pin_6);
            break;
        case FPGA_WAVE_SQUARE:
            GPIO_SetBits(GPIOB, GPIO_Pin_7);
            break;
        default:
            break;
    }
}

/* ==================================================================
 *  第二部分：USART2 初始化 —— 不变
 * ================================================================== */

void FPGA_InitRX(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef  NVIC_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate            = 115200;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Rx;
    USART_Init(USART2, &USART_InitStructure);

    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);

    NVIC_InitStructure.NVIC_IRQChannel                   = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 2;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    USART_Cmd(USART2, ENABLE);
}

/* ==================================================================
 *  第三部分：数据存储（三组独立数据 + 接收缓冲）
 *
 *  总 RAM ≈ 64+32+2+2+1+28 + 64+32+2+2+1 + 400+1+1
 *         + 256+1+1+2+1 ≈ 894 字节
 * ================================================================== */

/* ---- 扫频数据 ---- */
static uint32_t g_SweepFreq[FPGA_SWEEP_MAX];
static uint16_t g_SweepAmp [FPGA_SWEEP_MAX];
static uint16_t g_SweepCount = 0;
static uint16_t g_SweepPage  = 0;
static uint8_t  g_SweepTraceId = 0;
static uint8_t  g_SweepDirty = 1;
static char     g_CircuitType[28];

/* ---- 谐波数据 ---- */
static uint32_t g_HarmFreq[FPGA_HARM_MAX];
static uint16_t g_HarmAmp [FPGA_HARM_MAX];
static uint16_t g_HarmCount = 0;
static uint16_t g_HarmPage  = 0;
static uint8_t  g_HarmTraceId = 0;
static uint8_t  g_HarmDirty = 1;

/* ---- 波形像素 ---- */
static uint8_t  g_WavePixels[FPGA_WAVEFORM_PIXELS];
static volatile uint8_t g_WavePixelsReady = 0;
static uint8_t  g_WaveTraceId = 0;

/* ---- 接收状态机 ---- */
#define RX_LINE_MAX  256
static char    g_RxLine[RX_LINE_MAX];
static uint8_t g_RxLen = 0;

typedef enum {
    RX_STATE_TEXT = 0,
    RX_STATE_BIN_HDR1,
    RX_STATE_BIN_HDR2,
    RX_STATE_BIN_DATA
} RxState_t;

static RxState_t g_RxState = RX_STATE_TEXT;
static uint16_t  g_RxBinCount = 0;
static uint8_t   g_RxBinTraceId = 0;

/* ==================================================================
 *  第四部分：工具函数
 * ================================================================== */

static uint32_t FPGA_AToI(const char *s)
{
    uint32_t val = 0;
    while (*s >= '0' && *s <= '9')
    {
        val = val * 10u + (uint32_t)(*s - '0');
        s++;
    }
    return val;
}

/* 解析电压字符串 "1.00"（遇到 ';' 或行尾停止），返回 ×100 的整数 */
static uint16_t FPGA_ParseVoltage(char **ps)
{
    char *s = *ps;
    uint16_t intPart = 0, fracPart = 0;
    uint8_t  fracDigits = 0;

    while (*s >= '0' && *s <= '9')
    {
        intPart = intPart * 10u + (uint16_t)(*s - '0');
        s++;
    }
    if (*s == '.')
    {
        s++;
        while (*s >= '0' && *s <= '9')
        {
            fracPart = fracPart * 10u + (uint16_t)(*s - '0');
            fracDigits++;
            s++;
        }
    }
    while (fracDigits < 2) { fracPart *= 10u; fracDigits++; }
    while (*s && *s != ';') s++;
    *ps = s;
    return intPart * 100u + fracPart;
}

/* ==================================================================
 *  第五部分：行解析 —— 分流 + trace_id 提取
 * ================================================================== */

/* 从行尾提取 ";ID=nn" 中的 nn，未找到返回 0 */
static uint8_t FPGA_ExtractTraceId(char *line)
{
    uint8_t len = 0;
    char *p;
    for (p = line; *p; p++) len++;
    /* 从行尾回退找 ";ID=" */
    while (len >= 4)
    {
        if (line[len-1] == ';')
        {
            /* 看前面是否有 ID= */
            if (len >= 4 && line[len-4] == 'I' && line[len-3] == 'D' && line[len-2] == '=')
                return (uint8_t)FPGA_AToI(&line[len-1]);  /* 从 ';' 后即数字开始读？不，ID= 后是数字 */
            /* 修正：ID=nn; 格式中，nn 在 '=' 和 ';' 之间 */
            if (len >= 5)
            {
                uint8_t pos = len - 2;  /* 最后一个非';'字符 */
                /* 反向找 '=' */
                while (pos > 2)
                {
                    if (line[pos] == '=' && line[pos-1] == 'D' && line[pos-2] == 'I')
                        return (uint8_t)FPGA_AToI(&line[pos+1]);
                    pos--;
                }
            }
            break;
        }
        len--;
    }
    return 0;
}

/* 返回 ID= 之后数字串的起始位置，未找到返回 NULL */
static char *FPGA_FindIdValue(char *line)
{
    char *p = line;
    while (*p)
    {
        if (p[0] == 'I' && p[1] == 'D' && p[2] == '=')
            return p + 3;   /* 指向数字部分 */
        p++;
    }
    return 0;
}

/* 扫频解析：输入 "2nd_LPF(二阶低通);F00100V1.00;...;ID=12;" */
static void FPGA_ParseSweep(char *data)
{
    char *p = data;
    uint16_t i = 0;

    /* 读电路类型到 ';' */
    {
        uint8_t j = 0;
        while (*p && *p != ';' && j < sizeof(g_CircuitType) - 1)
            g_CircuitType[j++] = *p++;
        g_CircuitType[j] = '\0';
        if (*p == ';') p++;
    }

    /* 循环解析 F频率V电压; 数据点 */
    while (*p && i < FPGA_SWEEP_MAX)
    {
        /* 遇到 ID= 则停止 */
        if (p[0] == 'I' && p[1] == 'D' && p[2] == '=') break;
        /* 跳过 'F' */
        if (*p == 'F') p++;
        else { p++; continue; }

        g_SweepFreq[i] = FPGA_AToI(p);
        /* 跳过数字到 'V' */
        while (*p >= '0' && *p <= '9') p++;
        if (*p == 'V') p++;
        else break;

        g_SweepAmp[i] = FPGA_ParseVoltage(&p);
        if (*p == ';') p++;
        i++;
    }

    g_SweepCount = i;
    g_SweepPage  = 0;
    g_SweepDirty = 1;
}

/* 谐波解析：输入 "Base:00977Hz,1.00V;H2:02995Hz,0.40V;ID=13;" */
static void FPGA_ParseHarmonic(char *data)
{
    char *p = data;
    uint16_t i = 0;

    while (*p && i < FPGA_HARM_MAX)
    {
        /* 遇到 ID= 则停止 */
        if (p[0] == 'I' && p[1] == 'D' && p[2] == '=') break;
        /* 跳过名称到 ':' */
        while (*p && *p != ':') p++;
        if (*p == ':') p++;
        else break;

        g_HarmFreq[i] = FPGA_AToI(p);
        /* 跳过数字到 'H' */
        while (*p >= '0' && *p <= '9') p++;
        /* 跳过 "Hz," */
        if (p[0] == 'H' && p[1] == 'z' && p[2] == ',') p += 3;
        else break;

        g_HarmAmp[i] = FPGA_ParseVoltage(&p);
        if (*p == ';') p++;
        i++;
    }

    g_HarmCount = i;
    g_HarmPage  = 0;
    g_HarmDirty = 1;
}

/* 行解析入口 */
static void FPGA_ParseRxLine(char *line)
{
    uint8_t tid;
    char *idPos;

    if (line[0] == '\0') return;

    /* 先提取 trace_id */
    tid = 0;
    idPos = FPGA_FindIdValue(line);
    if (idPos) tid = (uint8_t)FPGA_AToI(idPos);

    /* 前缀分流 */
    if (strncmp(line, "TXT:TYPE:", 9) == 0)
    {
        g_SweepTraceId = tid;
        FPGA_ParseSweep(line + 9);
    }
    else if (strncmp(line, "TXT:HARMONICS:", 14) == 0)
    {
        g_HarmTraceId = tid;
        FPGA_ParseHarmonic(line + 14);
    }
    /* 其他前缀：忽略 */
}

/* ==================================================================
 *  第六部分：接收状态机
 * ================================================================== */

void FPGA_OnRxByte(uint8_t byte)
{
    switch (g_RxState)
    {
    case RX_STATE_TEXT:
        if (byte == 0xAA)
        {
            g_RxState = RX_STATE_BIN_HDR1;
        }
        else if (byte == '\n')
        {
            g_RxLine[g_RxLen] = '\0';
            g_RxLen = 0;
            FPGA_ParseRxLine(g_RxLine);
        }
        else if (byte != '\r')
        {
            if (g_RxLen < RX_LINE_MAX - 1)
                g_RxLine[g_RxLen++] = (char)byte;
        }
        break;

    case RX_STATE_BIN_HDR1:
        if (byte == 0x55)
        {
            g_RxState = RX_STATE_BIN_HDR2;
        }
        else
        {
            /* 误触发：回退 0xAA 和该字节到文本缓冲 */
            if (g_RxLen + 2 < RX_LINE_MAX)
            {
                g_RxLine[g_RxLen++] = (char)0xAA;
                g_RxLine[g_RxLen++] = (char)byte;
            }
            g_RxState = RX_STATE_TEXT;
        }
        break;

    case RX_STATE_BIN_HDR2:
        g_RxBinTraceId = byte;
        g_RxBinCount = 0;
        g_RxState = RX_STATE_BIN_DATA;
        break;

    case RX_STATE_BIN_DATA:
        if (g_RxBinCount < FPGA_WAVEFORM_PIXELS)
        {
            g_WavePixels[g_RxBinCount] = byte;
            g_RxBinCount++;
        }
        else
        {
            if (byte == 0xFF)
            {
                g_WaveTraceId = g_RxBinTraceId;
                g_WavePixelsReady = 1;
            }
            g_RxState = RX_STATE_TEXT;
        }
        break;
    }
}

/* ==================================================================
 *  第七部分：波形像素降采样
 * ================================================================== */

uint8_t FPGA_GetWavePixel(uint16_t index)
{
    uint16_t start, end, j, sum, count;

    if (index >= FPGA_WAVEFORM_DISPLAY) return 0;

    start = (uint16_t)(((uint32_t)index * FPGA_WAVEFORM_PIXELS) / FPGA_WAVEFORM_DISPLAY);
    end   = (uint16_t)(((uint32_t)(index + 1) * FPGA_WAVEFORM_PIXELS) / FPGA_WAVEFORM_DISPLAY);

    sum = 0; count = 0;
    for (j = start; j < end; j++)
    {
        sum += g_WavePixels[j];
        count++;
    }
    return (uint8_t)(sum / count);
}

uint8_t FPGA_WavePixelsReady(void)        { return g_WavePixelsReady; }
void    FPGA_ClearWavePixelsReady(void)   { g_WavePixelsReady = 0; }
uint8_t FPGA_GetWaveTraceId(void)         { return g_WaveTraceId; }

/* ==================================================================
 *  第八部分：扫频数据访问接口
 * ================================================================== */

uint16_t    FPGA_GetSweepCount(void)            { return g_SweepCount; }
uint8_t     FPGA_SweepHasData(void)             { return (g_SweepCount > 0) ? 1 : 0; }
const char *FPGA_GetCircuitType(void)           { return g_CircuitType; }
uint8_t     FPGA_GetSweepTraceId(void)          { return g_SweepTraceId; }

uint8_t FPGA_GetSweepPoint(uint16_t index, uint32_t *freq, uint16_t *amp)
{
    if (index >= g_SweepCount) return 0;
    *freq = g_SweepFreq[index];
    *amp  = g_SweepAmp [index];
    return 1;
}

void FPGA_SweepTableNextPage(void)
{
    uint16_t total = FPGA_SweepTableGetTotalPages();
    if (total == 0) return;
    if (g_SweepPage + 1 < total)
    {
        g_SweepPage++;
        g_SweepDirty = 1;
    }
}

void FPGA_SweepTablePrevPage(void)
{
    if (g_SweepPage > 0)
    {
        g_SweepPage--;
        g_SweepDirty = 1;
    }
}

uint16_t FPGA_SweepTableGetCurrentPage(void)    { return g_SweepPage + 1; }

uint16_t FPGA_SweepTableGetTotalPages(void)
{
    if (g_SweepCount == 0) return 0;
    return (g_SweepCount + FPGA_TABLE_ROWS_PER_PAGE - 1) / FPGA_TABLE_ROWS_PER_PAGE;
}

uint8_t FPGA_SweepTableIsDirty(void)            { return g_SweepDirty; }
void    FPGA_SweepTableClearDirty(void)         { g_SweepDirty = 0; }
void    FPGA_SweepTableSetDirty(void)           { g_SweepDirty = 1; }

/* ==================================================================
 *  第九部分：谐波数据访问接口
 * ================================================================== */

uint16_t FPGA_HarmGetPointCount(void)           { return g_HarmCount; }
uint8_t  FPGA_HarmTableHasData(void)            { return (g_HarmCount > 0) ? 1 : 0; }
uint8_t  FPGA_GetHarmTraceId(void)              { return g_HarmTraceId; }

uint8_t FPGA_HarmGetPoint(uint16_t index, uint32_t *freq, uint16_t *amp)
{
    if (index >= g_HarmCount) return 0;
    *freq = g_HarmFreq[index];
    *amp  = g_HarmAmp [index];
    return 1;
}

void FPGA_HarmTableNextPage(void)
{
    uint16_t total = FPGA_HarmTableGetTotalPages();
    if (total == 0) return;
    if (g_HarmPage + 1 < total)
    {
        g_HarmPage++;
        g_HarmDirty = 1;
    }
}

void FPGA_HarmTablePrevPage(void)
{
    if (g_HarmPage > 0)
    {
        g_HarmPage--;
        g_HarmDirty = 1;
    }
}

uint16_t FPGA_HarmTableGetCurrentPage(void)     { return g_HarmPage + 1; }

uint16_t FPGA_HarmTableGetTotalPages(void)
{
    if (g_HarmCount == 0) return 0;
    return (g_HarmCount + FPGA_TABLE_ROWS_PER_PAGE - 1) / FPGA_TABLE_ROWS_PER_PAGE;
}

uint8_t FPGA_HarmTableIsDirty(void)             { return g_HarmDirty; }
void    FPGA_HarmTableClearDirty(void)          { g_HarmDirty = 0; }
void    FPGA_HarmTableSetDirty(void)            { g_HarmDirty = 1; }

/* ==================================================================
 *  第十部分：测试数据生成
 * ================================================================== */

void FPGA_GenerateTestData(void)
{
    /* 模拟扫频：8 点，二阶低通响应 */
    uint32_t freqs[8] = {100, 233, 543, 1265, 2947, 6867, 16001, 37272};
    uint16_t amps[8]  = {100, 100, 100, 99, 86, 35, 3, 0};  /* ×100 的电压 */
    uint16_t i;

    for (i = 0; i < 8; i++)
    {
        g_SweepFreq[i] = freqs[i];
        g_SweepAmp[i]  = amps[i];
    }
    g_SweepCount = 8;
    g_SweepPage  = 0;
    g_SweepDirty = 1;

    /* 复制电路类型 */
    {
        const char *t = "2nd_LPF(Test)";
        uint8_t j;
        for (j = 0; t[j] && j < sizeof(g_CircuitType) - 1; j++)
            g_CircuitType[j] = t[j];
        g_CircuitType[j] = '\0';
    }
}

void FPGA_GenerateHarmTestData(void)
{
    /* 模拟谐波：基频 + 3次谐波 */
    g_HarmFreq[0] = 977;   g_HarmAmp[0] = 100;  /* Base: 977Hz, 1.00V */
    g_HarmFreq[1] = 2995;  g_HarmAmp[1] = 40;   /* H2:  2995Hz, 0.40V */
    g_HarmCount = 2;
    g_HarmPage  = 0;
    g_HarmDirty = 1;
}
