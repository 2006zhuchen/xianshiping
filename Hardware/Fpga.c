#include "Fpga.h"

/* ==================================================================
 *  第一部分：GPIO 波形控制（PB5/PB6/PB7）
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
 *  第二部分：USART2 接收 FPGA 扫频数据
 *
 *  波特率：115200
 *  格式：  频率,幅值\r\n
 *  例：    100,2500\r\n
 * ================================================================== */

static uint32_t g_FreqData[FPGA_MAX_POINTS];
static uint16_t g_AmpData [FPGA_MAX_POINTS];
static volatile uint16_t g_PointCount = 0;
static uint16_t g_TablePage = 0;

/* 谐波数据（pagewave 独立使用） */
static uint32_t g_HarmFreq[FPGA_HARM_MAX_POINTS];
static uint16_t g_HarmAmp [FPGA_HARM_MAX_POINTS];
static uint16_t g_HarmCount = 0;
static uint16_t g_HarmPage = 0;

/* 简单字符串转整数（不依赖 stdlib） */
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

/* 解析一行 "频率,幅值" 并存入数组 */
static void FPGA_ParseLine(char *line)
{
    char *comma = 0;
    uint8_t i;
    uint32_t freq;
    uint16_t amp;

    for (i = 0; line[i]; i++)
    {
        if (line[i] == ',')
        {
            comma = &line[i];
            break;
        }
    }
    if (comma == 0) return;
    if (g_PointCount >= FPGA_MAX_POINTS) return;

    *comma = '\0';
    freq = FPGA_AToI(line);
    amp  = (uint16_t)FPGA_AToI(comma + 1);

    g_FreqData[g_PointCount] = freq;
    g_AmpData [g_PointCount] = amp;
    g_PointCount++;
}

void FPGA_OnRxByte(uint8_t byte)
{
    static char lineBuf[32];
    static uint8_t lineLen = 0;

    if (byte == '\n' || lineLen >= sizeof(lineBuf) - 1)
    {
        lineBuf[lineLen] = '\0';
        lineLen = 0;
        if (lineBuf[0] != '\0')
            FPGA_ParseLine(lineBuf);
    }
    else if (byte != '\r')
    {
        lineBuf[lineLen++] = (char)byte;
    }
}

void FPGA_InitRX(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef  NVIC_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

    /* PA3 = USART2_RX，浮空输入 */
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* USART2 参数：115200 / 8数据位 / 1停止位 / 无校验 / 仅接收 */
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
 *  第三部分：测试数据生成
 *
 *  生成一组模拟的幅频特性数据，用于在没有 FPGA 时验证表格显示。
 *  频率范围：100Hz ~ 10000Hz，步进 100Hz，共 100 个点。
 *  幅值模拟一阶 RC 低通响应（截止频率 2000Hz，峰值 5000mV）。
 * ================================================================== */

void FPGA_GenerateTestData(void)
{
    uint32_t freq;
    uint16_t i = 0;

    /*
     * 纯整数模拟低通幅频特性，不依赖 math.h。
     * amp = 5000 * 2000 / sqrt(2000^2 + freq^2)，用整数近似。
     */
    for (freq = 100; freq <= 10000 && i < FPGA_MAX_POINTS; freq += 100)
    {
        uint32_t sq = freq * freq / 1000u;
        uint32_t denom = 4000u + sq;       /* 2000^2/1000 + freq^2/1000 */
        uint16_t amp = (uint16_t)(10000000u / denom);  /* 5000*2000 / denom */

        g_FreqData[i] = freq;
        g_AmpData[i] = amp;
        i++;
    }
    g_PointCount = i;
    g_TablePage = 0;
}

/* ==================================================================
 *  第四部分：表格翻页管理
 * ================================================================== */

uint16_t FPGA_GetPointCount(void)
{
    return g_PointCount;
}

uint8_t FPGA_GetPoint(uint16_t index, uint32_t *freq, uint16_t *amp)
{
    if (index >= g_PointCount) return 0;
    *freq = g_FreqData[index];
    *amp  = g_AmpData [index];
    return 1;
}

void FPGA_TableNextPage(void)
{
    uint16_t total = FPGA_TableGetTotalPages();
    if (total == 0) return;
    if (g_TablePage + 1 < total)
        g_TablePage++;
}

void FPGA_TablePrevPage(void)
{
    if (g_TablePage > 0)
        g_TablePage--;
}

uint16_t FPGA_TableGetCurrentPage(void)
{
    return g_TablePage + 1;
}

uint16_t FPGA_TableGetTotalPages(void)
{
    if (g_PointCount == 0) return 0;
    return (g_PointCount + FPGA_TABLE_ROWS_PER_PAGE - 1) / FPGA_TABLE_ROWS_PER_PAGE;
}

uint8_t FPGA_TableHasData(void)
{
    return (g_PointCount > 0) ? 1 : 0;
}

/* ==================================================================
 *  第五部分：谐波数据（pagewave 表格独立使用）
 *
 *  基频 50Hz，2~8次谐波幅值递减。
 * ================================================================== */

void FPGA_GenerateHarmTestData(void)
{
    uint16_t i;
    /* 基频 50Hz，幅值 5000；2次谐波 100Hz 幅值 2500；以此类推 */
    for (i = 0; i < 8 && i < FPGA_HARM_MAX_POINTS; i++)
    {
        uint16_t n = i + 1;
        g_HarmFreq[i] = 50u * n;
        g_HarmAmp[i]  = 5000u / n;
    }
    g_HarmCount = i;
    g_HarmPage = 0;
}

uint16_t FPGA_HarmGetPointCount(void)
{
    return g_HarmCount;
}

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
        g_HarmPage++;
}

void FPGA_HarmTablePrevPage(void)
{
    if (g_HarmPage > 0)
        g_HarmPage--;
}

uint16_t FPGA_HarmTableGetCurrentPage(void)
{
    return g_HarmPage + 1;
}

uint16_t FPGA_HarmTableGetTotalPages(void)
{
    if (g_HarmCount == 0) return 0;
    return (g_HarmCount + FPGA_TABLE_ROWS_PER_PAGE - 1) / FPGA_TABLE_ROWS_PER_PAGE;
}

uint8_t FPGA_HarmTableHasData(void)
{
    return (g_HarmCount > 0) ? 1 : 0;
}
