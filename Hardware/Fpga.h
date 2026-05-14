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
 * 硬件连接（USART2 输入 ← FPGA 扫频数据）：
 *   STM32 PA3  ←  FPGA TX
 *   STM32 GND  ←→ FPGA GND
 *
 * 电平含义：
 *   PB5=1  正弦波（SINE）
 *   PB6=1  三角波（TRI）
 *   PB7=1  方波  （SQUARE）
 *   同一时刻只有一根线为高电平
 *
 * 数据协议（ASCII，FPGA → STM32）：
 *   频率,幅值\r\n
 *   例：100,2500\r\n
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

/* ========== 扫频数据接收（USART2） ========== */

#define FPGA_TABLE_ROWS_PER_PAGE  4
#define FPGA_MAX_POINTS           2000
#define FPGA_HARM_MAX_POINTS      200

void     FPGA_InitRX(void);
void     FPGA_OnRxByte(uint8_t byte);

uint16_t FPGA_GetPointCount(void);
uint8_t  FPGA_GetPoint(uint16_t index, uint32_t *freq, uint16_t *amp);

void     FPGA_TableNextPage(void);
void     FPGA_TablePrevPage(void);
uint16_t FPGA_TableGetCurrentPage(void);
uint16_t FPGA_TableGetTotalPages(void);
uint8_t  FPGA_TableHasData(void);

/* ========== 测试用：生成模拟扫频数据 ========== */
void     FPGA_GenerateTestData(void);

/* ========== 谐波数据（pagewave 表格用） ========== */

void     FPGA_GenerateHarmTestData(void);

uint16_t FPGA_HarmGetPointCount(void);
uint8_t  FPGA_HarmGetPoint(uint16_t index, uint32_t *freq, uint16_t *amp);

void     FPGA_HarmTableNextPage(void);
void     FPGA_HarmTablePrevPage(void);
uint16_t FPGA_HarmTableGetCurrentPage(void);
uint16_t FPGA_HarmTableGetTotalPages(void);
uint8_t  FPGA_HarmTableHasData(void);

#endif
