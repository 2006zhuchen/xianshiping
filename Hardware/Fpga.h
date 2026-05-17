#ifndef __FPGA_H
#define __FPGA_H

#include "stm32f10x.h"

/*
 * Fpga.h  —— FPGA 通信与控制接口
 *
 * 硬件连接（GPIO 输出 → FPGA 波形选择）：
 *   STM32 PB5  ←→  FPGA 波形选择位0
 *   STM32 PB6  ←→  FPGA 波形选择位1
 *   STM32 PB7  ←→  FPGA 波形选择位2
 *   STM32 GND  ←→  FPGA GND
 *
 * 硬件连接（USART2 输入 ← FPGA 数据）：
 *   STM32 PA3  ←  FPGA TX
 *   STM32 GND  ←→ FPGA GND
 *
 * 数据协议（USART2 @ 115200）：
 *   扫频文本: TXT:TYPE:电路类型;F频率V电压;...;ID=nn;\r\n
 *   谐波文本: TXT:HARMONICS:名称:频率Hz,电压V;...;ID=nn;\r\n
 *   波形二进制: 0xAA 0x55 trace_id [400×uint8] 0xFF
 */

/* ========== 波形控制（GPIO） ========== */

typedef enum
{
    FPGA_WAVE_SINE   = 0,
    FPGA_WAVE_TRI    = 1,
    FPGA_WAVE_SQUARE = 2
} FPGA_WaveType;

void FPGA_Init(void);
void FPGA_SetWave(FPGA_WaveType wave);

/* ========== 数据接收（USART2）—— 公共 ========== */

#define FPGA_TABLE_ROWS_PER_PAGE  4
#define FPGA_SWEEP_MAX            16
#define FPGA_HARM_MAX             16
#define FPGA_WAVEFORM_PIXELS      400
#define FPGA_WAVEFORM_DISPLAY     64

void FPGA_InitRX(void);
void FPGA_OnRxByte(uint8_t byte);

/* ========== 扫频数据（pageparam 表格用） ========== */

uint16_t    FPGA_GetSweepCount(void);
uint8_t     FPGA_GetSweepPoint(uint16_t index, uint32_t *freq, uint16_t *amp);
uint8_t     FPGA_SweepHasData(void);
const char *FPGA_GetCircuitType(void);
uint8_t     FPGA_GetSweepTraceId(void);

void        FPGA_SweepTableNextPage(void);
void        FPGA_SweepTablePrevPage(void);
uint16_t    FPGA_SweepTableGetCurrentPage(void);
uint16_t    FPGA_SweepTableGetTotalPages(void);

/* 脏标记（UI 用，避免重复发送表格） */
uint8_t FPGA_SweepTableIsDirty(void);
void    FPGA_SweepTableClearDirty(void);
void    FPGA_SweepTableSetDirty(void);

/* ========== 谐波数据（pagewave 表格用） ========== */

uint16_t FPGA_HarmGetPointCount(void);
uint8_t  FPGA_HarmGetPoint(uint16_t index, uint32_t *freq, uint16_t *amp);
uint8_t  FPGA_HarmTableHasData(void);
uint8_t  FPGA_GetHarmTraceId(void);

void     FPGA_HarmTableNextPage(void);
void     FPGA_HarmTablePrevPage(void);
uint16_t FPGA_HarmTableGetCurrentPage(void);
uint16_t FPGA_HarmTableGetTotalPages(void);

uint8_t FPGA_HarmTableIsDirty(void);
void    FPGA_HarmTableClearDirty(void);
void    FPGA_HarmTableSetDirty(void);

/* ========== 波形像素（pagewave s0 用） ========== */

uint8_t FPGA_WavePixelsReady(void);
void    FPGA_ClearWavePixelsReady(void);
uint8_t FPGA_GetWavePixel(uint16_t index);   /* 降采样 400→64 */
uint8_t FPGA_GetWaveTraceId(void);

/* ========== 测试数据生成 ========== */

void FPGA_GenerateTestData(void);
void FPGA_GenerateHarmTestData(void);

#endif
