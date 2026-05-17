/*
 * main.c  - 多页面串口屏框架（支持多个页面、多种控件的模板）
 *
 * =====================================================
 * 这个框架的核心思路：
 * =====================================================
 * 1. 定义页面（PAGE_HOME/PAGE_WAVE/PAGE_PARAM...）
 * 2. 定义业务数据结构（UI_Data_t，包含温度/速度/波形等）
 * 3. 定义各页面的刷新函数（UI_UpdateHome/UI_UpdateWave...）
 *    - 每个函数只更新该页面的控件
 *    - 只当切到该页时才调用（节省串口带宽）
 * 4. 主循环：采样数据 → 根据当前页刷新控件 → 延时
 *
 * =====================================================
 * 你需要修改的 5 个地方：
 * =====================================================
 * [修改1] 页面号：PAGE_HOME/PAGE_WAVE/PAGE_PARAM 的值要和你USART HMI里的页面ID一致
 * [修改2] 文本/数字组件名：t0/n0 等要改成你USART HMI里的组件名
 * [修改3] 数据采样部分：Data_Simulate() 改成你的真实传感器读取
 * [修改4] 波形ID：add 1,0,x 里的"1"改成你的波形组件ID
 * [修改5] 按键切页逻辑：可改成按屏幕按钮切页或其他逻辑
 */

#include "stm32f10x.h"
#include "Delay.h"
#include "OLED.h"
#include "Seruak.h"
#include "Key.h"
#include "Fpga.h"
#include <stdio.h>
#include <string.h>

/* =1 时使用板载按键(PB1/PB11)切页；=0 时只根据屏幕 sendme 回传同步页面 */
#define USE_BOARD_KEY_PAGE_SWITCH 0

/* =1 时生成模拟扫频数据，测试表格显示；=0 时等待真实 FPGA 数据 */
#define FPGA_TEST_MODE 0

/* 陶晶驰屏上放"上一页/下一页"按钮的实际页面号（从串口回传 p=3 看出来） */
#define TJC_TABLE_TOUCH_PAGE 3

/* pagewave 谐波表翻页按钮的组件 ID */
#define TJC_WAVE_BTN_NEXT_ID  5   /* b2: 下一页 */
#define TJC_WAVE_BTN_PREV_ID  4   /* b1: 上一页 */

/*
 * ====================== 正弦波显示参数（给 s0 用） ======================
 * 组件ID说明：
 * 1) 组件"名字"是 s0（你在页面上看到的名称）
 * 2) add 指令使用的是"组件ID(数字)"，不是名字
 * 3) 如果波形不出图，优先检查 TJC_WAVE_COMP_ID 是否与你页面里的 s0 组件ID一致
 */
#define TJC_WAVE_COMP_ID 3
#define TJC_WAVE_CHANNEL 0
#define TJC_SINE_LUT_SIZE 64

/* 0~255 的一个周期正弦查表（64点） */
static const uint8_t g_SineLut[TJC_SINE_LUT_SIZE] =
{
	128,140,153,165,177,188,199,209,218,226,234,240,245,250,253,254,
	255,254,253,250,245,240,234,226,218,209,199,188,177,165,153,140,
	128,115,102,90,78,67,56,46,37,29,21,15,10,5,2,1,
	0,1,2,5,10,15,21,29,37,46,56,67,78,90,102,115
};

/* =====================================================
 * [修改1] 页面定义 - 改成你的USART HMI里的页面号
 * =====================================================
 * 在USART HMI里，左上角会看到 page0, page1, page2...
 * 这里的PAGE_HOME/PAGE_WAVE/PAGE_PARAM就对应那些页面ID
 */
typedef enum
{
	PAGE_HOME = 0,    /* 首页，显示速度、转速等 */
	PAGE_WAVE = 1,    /* 波形页，显示实时波形图 */
	PAGE_PARAM = 2    /* 参数页，显示温度、电压等 */
	/* 如果你有更多页面，继续添加，例如：PAGE_ALARM = 3 */
} UI_Page_t;

/* =====================================================
 * [修改2a] 业务数据结构 - 根据你的需求添加/删除字段
 * =====================================================
 * 这个结构体包含所有可能在各页面显示的数据
 * 字段说明：
 *   frequency     - 速度（单位自定，显示在首页）
 *   rpm       - 转速（显示在首页）
 *   temp      - 温度（显示在参数页）
 *   response      - 电压（显示在参数页）
 *   waveSample - 波形数据（0~255，显示在波形页）
 *
 * 如果你需要显示其他数据（如湿度、压力等），
 * 在这里添加新字段，然后在各页面刷新函数里调用 TJC_SetXxx() 更新
 */
typedef struct
{
	int32_t frequency;         /* 速度 */
	int32_t rpm;           /* 转速 */
	float temp;            /* 温度 */
	float response;            /* 电压 */
	uint8_t waveSample;    /* 波形采样值 (0~255) */
} UI_Data_t;

/* 全局变量：记录当前显示的页面和业务数据
   g_Page 会在中断里更新，所以要加 volatile */
static volatile UI_Page_t g_Page = PAGE_HOME;
static UI_Data_t g_Data = {0};
static volatile uint8_t g_PageSyncReady = 0;
static volatile uint8_t g_WaveNeedClear = 1;
static uint8_t g_SineIndex = 0;
/* touch debug: store last touch frame from screen for diagnostics (set in ISR) */
static volatile uint8_t g_LastTouchPage = 0;
static volatile uint8_t g_LastTouchComp = 0;
static volatile uint8_t g_LastTouchEvent = 0;
static volatile uint8_t g_TouchReady = 0;
/*
 * UI_OnRxByte() - 串口接收字节解析（给 USART1_IRQHandler 调用）
 *
 * 目标：解析陶晶驰 sendme 返回帧：
 *   0x66, pageId, 0xFF, 0xFF, 0xFF
 *
 * 上位机要做的配置（非常关键）：
 *   在每个页面的"页面初始化事件"里写：sendme
 *   这样每次切页，屏幕都会回传当前页号，MCU 就能同步 g_Page。
 */
#define RX_BUF_SIZE 16

void UI_OnRxByte(uint8_t byte)
{
	static uint8_t buf[RX_BUF_SIZE];
	static uint8_t len = 0;
	static uint8_t ffCnt = 0;

	/* 缓冲区溢出保护 */
	if (len >= RX_BUF_SIZE)
	{
		len = 0;
		ffCnt = 0;
		return;
	}

	buf[len++] = byte;

	/* 计数连续 0xFF */
	if (byte == 0xFF)
		ffCnt++;
	else
		ffCnt = 0;

	/* 收到帧尾（3 个连续 0xFF） */
	if (ffCnt >= 3 && len >= 3)
	{
		uint8_t dataLen = len - 3;  /* 去掉尾部 3 个 0xFF */

		if (dataLen >= 2 && buf[0] == 0x66)
		{
			/* 0x66 页面同步帧：66 pageId */
			UI_Page_t newPage = (UI_Page_t)buf[1];
			g_Page = newPage;
			if (newPage == PAGE_WAVE)
			{
				g_WaveNeedClear = 1;
				g_SineIndex = 0;
				FPGA_HarmTableSetDirty();
			}
			else if (newPage == PAGE_PARAM)
			{
				FPGA_SweepTableSetDirty();
			}
			g_PageSyncReady = 1;
		}
		else if (dataLen >= 4 && buf[0] == 0x65)
		{
			/* 0x65 触摸事件帧：65 pageId compId event */
			uint8_t pageId  = buf[1];
			uint8_t compId  = buf[2];
			uint8_t eventId = buf[3];

			/* store last touch frame for main-loop debug display (ISR-safe small writes) */
			g_LastTouchPage = pageId;
			g_LastTouchComp = compId;
			g_LastTouchEvent = eventId;
			g_TouchReady = 1;

			/* 只响应按下或松开事件，且仅在表格按钮所在的实际页面上 */
			if ((eventId == 0x00 || eventId == 0x01) && pageId == TJC_TABLE_TOUCH_PAGE)
			{
				switch (compId)
				{
					case 8: FPGA_SetWave(FPGA_WAVE_SINE);   break;
					case 6: FPGA_SetWave(FPGA_WAVE_TRI);    break;
					case 7: FPGA_SetWave(FPGA_WAVE_SQUARE); break;
					case 9:  FPGA_SweepTableNextPage(); break;
					case 10: FPGA_SweepTablePrevPage(); break;
					default: break;
				}

			}

			/* pagewave 谐波表翻页: b1=下一页, b2=上一页 */
			if (pageId == PAGE_WAVE)
			{
				if (compId == TJC_WAVE_BTN_NEXT_ID)
					FPGA_HarmTableNextPage();
				else if (compId == TJC_WAVE_BTN_PREV_ID)
					FPGA_HarmTablePrevPage();
			}
		}

		/* 重置缓冲区，等下一帧 */
		len = 0;
		ffCnt = 0;
	}
}

/* =====================================================
 * UI_SetPage() - 页面切换函数
 * =====================================================
 * 参数：page - 要跳转的页面编号（PAGE_HOME/PAGE_WAVE/PAGE_PARAM）
 * 作用：
 *   1. 发送 TJC_SetPage(page) 指令给屏幕
 *   2. g_Page 不在这里直接改，而是等待 sendme 回传后再更新
 *      （避免"屏幕实际页"和"MCU软件页"短暂不一致）
 * 何时调用：
 *   - 用户按下"下一页"按钮时
 *   - 程序需要主动切页时
 */
static void UI_SetPage(UI_Page_t page)
{
	TJC_SetPage((uint8_t)page);
}

/* =====================================================
 * UI_UpdateHome() - 首页刷新函数
 * =====================================================
 * 参数：d - 指向 UI_Data_t 的指针，包含最新的业务数据
 * 作用：
 *   1. 取出 d 里的数据
 *   2. 用 snprintf() 格式化成字符串（如"SPD:120"）
 *   3. 用 TJC_SetText() / TJC_SetNum() 发送给屏幕
 * 调用时机：
 *   - 每次主循环刷新（每 100ms）
 *   - 只要当前页面是 PAGE_HOME，就会执行
 *
 * 你需要改的地方：
 *   - 把"t0""n0"改成你USART HMI里的组件名
 *     例如你首页有组件 temp_label（显示温度），那就改成 TJC_SetText("temp_label", text);
 *   - 把"SPD:xxx"改成你想要的显示文本格式
 *   - 如果需要显示更多数据（如时间、状态等），继续添加 TJC_SetXxx() 调用
 */
static void UI_UpdateHome(const UI_Data_t *d)
{
	char text[32];

	/* 格式化速度文本 */
	snprintf(text, sizeof(text), "SPD:%d", (int)d->frequency);
	/* 修改1: "t0" 改成你USART HMI里首页的文本组件名 */
	TJC_SetText("t0", text);

	/* 设置转速数值 */
	/* 修改2: "n0" 改成你USART HMI里首页的数字组件名 */
	TJC_SetNum("n0", d->rpm);

	/* 如果你还有其他要显示的数据，继续添加：
	   例如：显示警报状态
	   char status[16];
	   snprintf(status, sizeof(status), "STATUS:%s", d->alarm ? "ALARM" : "OK");
	   TJC_SetText("tStatus", status);
	*/
}

/* =====================================================
 * UI_UpdateWave() - 波形页刷新函数
 * =====================================================
 * 参数：d - 指向 UI_Data_t 的指针，包含最新的波形采样值
 * 作用：
 *   1. 取出 d->waveSample（0~255的采样值）
 *   2. 发送 "add 波形ID,通道,值" 指令到屏幕
 *   3. 屏幕的波形控件会实时更新曲线
 * 调用时机：
 *   - 每次主循环刷新（每 100ms）
 *   - 只要当前页面是 PAGE_WAVE，就会执行
 *
 * 波形控件协议（TJC屏幕）：
 *   add <波形ID>,<通道>,<数值>
 *   - 波形ID: 你在USART HMI里为波形组件设置的ID（通常0~n）
 *   - 通道: 0/1/2/3 等（一个波形可显示多条曲线）
 *   - 数值: 0~255（对应波形的垂直显示范围）
 */

static void UI_UpdateWaveTable(void);

/* 波形分批灌屏状态 */
#define WAVE_BATCH_SIZE  8
static uint8_t g_WaveSendIndex = 0;
static uint8_t g_WaveSendActive = 0;
static uint8_t g_WaveNeedClrBatch = 0;

/*
 * 你需要改的地方：
 *   - "1"改成你的波形组件ID
 *     例如你在USART HMI里波形控件ID是2，就改成 add 2,0,%d
 *   - "0"是通道号，如果你只需要显示1条曲线，保持为0
 *     如果要显示多条（如3轴加速度），改成 0/1/2
 */
static void UI_UpdateWave(const UI_Data_t *d)
{
	char cmd[32];
	(void)d;

	/* ---- 分支 1：FPGA 波形就绪，启动分批灌屏 ---- */
	if (FPGA_WavePixelsReady())
	{
		FPGA_ClearWavePixelsReady();
		g_WaveSendIndex  = 0;
		g_WaveSendActive = 1;
		g_WaveNeedClrBatch = 1;
	}

	/* ---- 分支 2：分批发送（每次 WAVE_BATCH_SIZE 个点）---- */
	if (g_WaveSendActive)
	{
		uint8_t i;

		if (g_WaveNeedClrBatch)
		{
			snprintf(cmd, sizeof(cmd), "cle %d,%d",
			         TJC_WAVE_COMP_ID, TJC_WAVE_CHANNEL);
			TJC_SendCommand(cmd);
			g_WaveNeedClrBatch = 0;
		}

		for (i = 0; i < WAVE_BATCH_SIZE && g_WaveSendIndex < FPGA_WAVEFORM_DISPLAY; i++)
		{
			uint8_t y = FPGA_GetWavePixel(g_WaveSendIndex);
			snprintf(cmd, sizeof(cmd), "add %d,%d,%d",
			         TJC_WAVE_COMP_ID, TJC_WAVE_CHANNEL, y);
			TJC_SendCommand(cmd);
			g_WaveSendIndex++;
		}

		if (g_WaveSendIndex >= FPGA_WAVEFORM_DISPLAY)
			g_WaveSendActive = 0;

		UI_UpdateWaveTable();
		return;
	}

	/* ---- 分支 3：无 FPGA 波形，退化到正弦 LUT ---- */
	if (!FPGA_HarmTableHasData())
	{
		uint8_t y;

		if (g_WaveNeedClear)
		{
			snprintf(cmd, sizeof(cmd), "cle %d,%d",
			         TJC_WAVE_COMP_ID, TJC_WAVE_CHANNEL);
			TJC_SendCommand(cmd);
			g_WaveNeedClear = 0;
		}

		y = g_SineLut[g_SineIndex];
		g_SineIndex++;
		if (g_SineIndex >= TJC_SINE_LUT_SIZE)
			g_SineIndex = 0;

		snprintf(cmd, sizeof(cmd), "add %d,%d,%d",
		         TJC_WAVE_COMP_ID, TJC_WAVE_CHANNEL, y);
		TJC_SendCommand(cmd);
	}

	UI_UpdateWaveTable();
}

/* =====================================================
 * UI_UpdateTable() - 表格刷新函数
 * =====================================================
 * 从 FPGA 数据数组中取当前页的行，拼成表格字符串发给屏幕。
 * 调用时机：翻页按钮按下（g_TableDirty == 1）时由 UI_UpdateParam 调用。
 * 屏幕组件：需在 page2 放一个大文本框（t0），字体选等宽。
 */
static void UI_UpdateTable(void)
{
	char table[256];
	char col1[16], col2[16];
	char info[32];
	uint16_t i, start, end, total, curPage;
	int len;

	if (!FPGA_SweepTableIsDirty())
		return;

	if (!FPGA_SweepHasData())
	{
		TJC_SetText("t0", "NoData");
		TJC_SetText("t1", "");
		TJC_SetText("t2", "");
		FPGA_SweepTableClearDirty();
		return;
	}

	total   = FPGA_GetSweepCount();
	curPage = FPGA_SweepTableGetCurrentPage();
	start   = (curPage - 1) * FPGA_TABLE_ROWS_PER_PAGE;
	end     = start + FPGA_TABLE_ROWS_PER_PAGE;
	if (end > total) end = total;

	/* 电路类型发到 t2 */
	TJC_SetText("t2", (char *)FPGA_GetCircuitType());

	TJC_Table_FormatCell(col1, " Freq ", 7);
	TJC_Table_FormatCell(col2, " Vout ", 6);
	len = snprintf(table, sizeof(table), "%s%s\r\n", col1, col2);

	for (i = start; i < end; i++)
	{
		uint32_t freq;
		uint16_t amp;
		FPGA_GetSweepPoint(i, &freq, &amp);
		TJC_Table_FormatFreq5(col1, freq, 7);
		TJC_Table_FormatVolt (col2, amp,  6);
		len += snprintf(table + len, sizeof(table) - len,
		                "%s%s\r\n", col1, col2);
	}

	TJC_SetText("t0", table);

	snprintf(info, sizeof(info), "Pg %d/%d #%d",
	         (int)curPage,
	         (int)FPGA_SweepTableGetTotalPages(),
	         (int)FPGA_GetSweepTraceId());
	TJC_SetText("t1", info);

	FPGA_SweepTableClearDirty();
}


/* =====================================================
 * UI_UpdateWaveTable() - pagewave 谐波表格刷新函数
 * =====================================================
 * 拼 3 列谐波表：谐波次数 | 频率 | 幅度
 * 每页 4 行，发到 pagewave 的 t0。
 * b1 / b2 按钮翻页，弹起事件写：
 *   b1: printh 65 01 04 00 FF FF FF
 *   b2: printh 65 01 05 00 FF FF FF
 */
static void UI_UpdateWaveTable(void)
{
	char table[256];
	char col1[12], col2[12], col3[12];
	char info[32];
	uint16_t i, start, end, total, curPage;
	int len;

	if (!FPGA_HarmTableIsDirty())
		return;

	if (!FPGA_HarmTableHasData())
	{
		TJC_SetText("t0", "NoHarmData");
		TJC_SetText("t1", "");
		FPGA_HarmTableClearDirty();
		return;
	}

	total   = FPGA_HarmGetPointCount();
	curPage = FPGA_HarmTableGetCurrentPage();
	start   = (curPage - 1) * FPGA_TABLE_ROWS_PER_PAGE;
	end     = start + FPGA_TABLE_ROWS_PER_PAGE;
	if (end > total) end = total;

	TJC_Table_FormatCell(col1, "Harm", 5);
	TJC_Table_FormatCell(col2, " Freq ", 7);
	TJC_Table_FormatCell(col3, " Vout ",  6);
	len = snprintf(table, sizeof(table), "%s%s%s\r\n", col1, col2, col3);

	for (i = start; i < end; i++)
	{
		uint32_t freq;
		uint16_t amp;
		uint8_t harm = i + 1;
		FPGA_HarmGetPoint(i, &freq, &amp);
		TJC_Table_FormatNum(col1, (int32_t)harm, 5);
		TJC_Table_FormatFreq5(col2, freq, 7);
		TJC_Table_FormatVolt (col3, amp,  6);
		len += snprintf(table + len, sizeof(table) - len,
		                "%s%s%s\r\n", col1, col2, col3);
	}

	TJC_SetText("t0", table);

	snprintf(info, sizeof(info), "Pg %d/%d #%d",
	         (int)curPage,
	         (int)FPGA_HarmTableGetTotalPages(),
	         (int)FPGA_GetHarmTraceId());
	TJC_SetText("t1", info);

	FPGA_HarmTableClearDirty();
}

/* =====================================================
 * UI_UpdateParam() - 参数页刷新函数
 * =====================================================
 * 参数：d - 指向 UI_Data_t 的指针，包含最新的业务数据
 * 作用：
 *   1. 取出 d 里的温度/电压等参数
 *   2. 用 TJC_SetText() / TJC_SetFloat() 发送给屏幕
 *   3. 屏幕的参数页会更新显示
 * 调用时机：
 *   - 每次主循环刷新（每 100ms）
 *   - 只要当前页面是 PAGE_PARAM，就会执行
 *
 * TJC_SetFloat() 说明：
 *   - 用来显示浮点数（如温度 26.5℃）
 *   - 内部会把小数乘100变成整数发送
 *   - 屏幕端数字组件属性里要设"小数点位数=1"才能显示成26.5
 *
 * 你需要改的地方：
 *   - "t1""n1"改成你USART HMI里参数页的组件名
 *   - "T=%.1fC"改成你要的显示格式
 *   - 如需显示更多参数，继续添加 TJC_SetXxx() 调用
 */
static void UI_UpdateParam(const UI_Data_t *d)
{
	UI_UpdateTable();

	/* 设置电压数值（浮点） */
	/* 修改6: "n1"改成你USART HMI里参数页的电压数字组件名 */
	TJC_SetFloat("n0", d->frequency);
	TJC_SetFloat("n1", d->response);

	/* 如果你还有其他参数要显示（如湿度、压力、时间等），继续添加：
	   例如：
	   snprintf(text, sizeof(text), "H=%.0f%%", d->humidity);
	   TJC_SetText("t2", text);
	   
	   TJC_SetNum("n2", d->pressure);
	*/
}

/* =====================================================
 * UI_Process() - 页面刷新分发函数
 * =====================================================
 * 参数：d - 指向 UI_Data_t 的指针
 * 作用：
 *   根据当前页面 g_Page，调用对应的刷新函数
 *   确保只有当前页的控件会被更新（节省串口带宽）
 * 调用地点：主循环中，数据采样后立即调用
 * 
 * 注意：
 *   如果你增加了新页面，记得在这里添加对应的 case 分支
 *   例如：case PAGE_ALARM: UI_UpdateAlarm(d); break;
 */
static void UI_Process(const UI_Data_t *d)
{
	switch (g_Page)
	{
		case PAGE_HOME:  UI_UpdateHome(d);  break;
		case PAGE_WAVE:  UI_UpdateWave(d);  break;
		case PAGE_PARAM: UI_UpdateParam(d); break;
		default: break;
	}
}

/* =====================================================
 * Data_Simulate() - 业务数据模拟函数（测试用）
 * =====================================================
 * 参数：d - 指向 UI_Data_t 的指针
 * 作用：
 *   模拟生成不断变化的数据，用来测试页面/控件是否正常显示
 *   实际项目中应该替换成真实的传感器读取逻辑
 *
 * 修改7: 这里改成你的真实数据采集
 * =====================================================
 * 例如，你有 ADC、温度传感器等硬件，可以改成：
 * 
 *   static void Data_Update(UI_Data_t *d)
 *   {
 *       d->frequency = ADC_GetChannelValue(0);        // 从ADC通道0读速度
 *       d->rpm = Motor_GetRPM();                  // 从电机模块读转速
 *       d->temp = TempSensor_Read();              // 从温度传感器读
 *       d->response = ADC_GetChannelValue(1);         // 从ADC通道1读电压
 *       d->waveSample = get_waveform_sample();    // 从某个缓冲读波形数据
 *   }
 *
 *   然后在主循环里改成：Data_Update(&g_Data);
 */
static void Data_Simulate(UI_Data_t *d)
{
	static int32_t cnt = 0;

	/* 模拟数据，每次调用都会缓慢变化 */
	cnt++;
	d->frequency = (cnt % 200);                            /* 速度在0~199之间循环 */
	d->rpm = 1200 + (cnt % 800);                       /* 转速在1200~2000之间循环 */
	d->temp = 25.0f + (float)(cnt % 100) * 0.1f;      /* 温度在25~35℃之间循环 */
	d->response = 12.0f + (float)(cnt % 30) * 0.01f;      /* 电压在12~12.3V之间循环 */
	d->waveSample = (uint8_t)(cnt % 256);             /* 波形采样在0~255之间循环 */
}

/* =====================================================
 * main() - 主函数
 * =====================================================
 * 程序流程：
 *   1. 硬件初始化（OLED/按键/串口）
 *   2. 等待屏幕启动（3秒）
 *   3. 跳转到首页
 *   4. 无限循环：
 *      a) 读取按键决定是否切页
 *      b) 采集/模拟业务数据
 *      c) 根据当前页刷新屏幕控件
 *      d) 延时 100ms
 */
int main(void)
{
#if USE_BOARD_KEY_PAGE_SWITCH
	uint8_t keyNum;
#endif

	/* 第1步：硬件初始化 */
	OLED_Init();                       /* 初始化OLED小屏（调试显示用） */
#if USE_BOARD_KEY_PAGE_SWITCH
	Key_Init();                        /* 初始化板载按键（PB1/PB11） */
#endif
	Serial_Init(9600);                 /* 初始化USART1，波特率9600（必须和屏幕一致） */
	FPGA_Init();                       /* 初始化FPGA波形选择引脚（PB5/PB6/PB7） */
	FPGA_InitRX();                     /* 初始化USART2，接收FPGA扫频数据 */
#if FPGA_TEST_MODE
	FPGA_GenerateTestData();           /* generate test sweep data for table */
	FPGA_GenerateHarmTestData();        /* generate test harmonic data for wave table */
#endif

	/* 显示启动信息 */
	OLED_ShowString(1, 1, "TJC UI FRAME");
	Delay_ms(3000);                    /* wait for screen boot */

	/* 第2步：初始化UI状态 */
	UI_SetPage(PAGE_HOME);             /* 切到首页 */
	TJC_SendCommand("sendme");         /* 主动请求一次当前页，校准 g_Page */

	/* =====================================================
	 * 第3步：主循环（forever）
	 * =====================================================
	 * 修改8: 页面切换逻辑
	 *   当前用按键实现（KEY1=下一页，KEY2=回首页）
	 *   如果你配置了屏幕按钮回传，可以改成处理屏幕按钮事件
	 *   例如：
	 *     if (screen_button_pressed == NEXT_PAGE_BUTTON)
	 *         UI_SetPage((UI_Page_t)((g_Page + 1) % 3));  // 循环翻页
	 *
	 * 修改9: 数据采集
	 *   当前用 Data_Simulate() 模拟数据
	 *   改成你的真实数据读取函数，见上面的注释说明
	 */
	while (1)
	{
#if USE_BOARD_KEY_PAGE_SWITCH
		/* 读取按键，实现页面切换 */
		keyNum = Key_GetNum();
		if (keyNum == 1)               /* KEY1: 下一页 */
		{
			if (g_Page == PAGE_HOME)      UI_SetPage(PAGE_WAVE);
			else if (g_Page == PAGE_WAVE) UI_SetPage(PAGE_PARAM);
			else                          UI_SetPage(PAGE_HOME);
		}
		else if (keyNum == 2)          /* KEY2: 回到首页 */
		{
			UI_SetPage(PAGE_HOME);
		}
#endif

		/* 采集业务数据（当前用模拟，实际项目改成真实采样） */
		Data_Simulate(&g_Data);

		/* 根据当前页刷新对应的屏幕控件 */
		UI_Process(&g_Data);

		/* 显示当前页号在OLED上（调试用） */
		OLED_ShowNum(2, 1, (uint32_t)g_Page, 1);
		if (g_PageSyncReady)
		{
			OLED_ShowString(3, 1, "Page Sync OK ");
			g_PageSyncReady = 0;
		}
		{
			/* FPGA 数据调试：第3行显数据条数，第4行显当前页 */
			char dbg[16];
			snprintf(dbg, sizeof(dbg), "D:%-5d", (int)FPGA_GetSweepCount());
			OLED_ShowString(3, 1, dbg);
			snprintf(dbg, sizeof(dbg), "Pg:%-3d/%-3d",
			         (int)FPGA_SweepTableGetCurrentPage(),
			         (int)FPGA_SweepTableGetTotalPages());
			OLED_ShowString(4, 1, dbg);

			/* 如果有触摸回传，显示最后一次触摸的 page/comp/event（用于诊断） */
			if (g_TouchReady)
			{
				/* 不覆盖屏幕上用于显示页码的 t1，直接通过串口输出调试信息 */
				Serial_Printf("T: p=%d c=%d e=%d\r\n", (int)g_LastTouchPage, (int)g_LastTouchComp, (int)g_LastTouchEvent);
				g_TouchReady = 0;
			}
		}

		/* 延时100ms再处理下一次 */
		Delay_ms(100);
	}
}
