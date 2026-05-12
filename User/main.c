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
#include <stdio.h>

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
 *   volt      - 电压（显示在参数页）
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
	float volt;            /* 电压 */
	uint8_t waveSample;    /* 波形采样值 (0~255) */
} UI_Data_t;

/* 全局变量：记录当前显示的页面和业务数据 */
static UI_Page_t g_Page = PAGE_HOME;
static UI_Data_t g_Data = {0};

/* =====================================================
 * UI_SetPage() - 页面切换函数
 * =====================================================
 * 参数：page - 要跳转的页面编号（PAGE_HOME/PAGE_WAVE/PAGE_PARAM）
 * 作用：
 *   1. 更新全局状态 g_Page（告诉主循环要刷新哪个页面）
 *   2. 发送 TJC_SetPage(page) 指令给屏幕
 *   3. 屏幕收到后会切到对应页面
 * 何时调用：
 *   - 用户按下"下一页"按钮时
 *   - 程序需要主动切页时
 */
static void UI_SetPage(UI_Page_t page)
{
	g_Page = page;
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
 *
 * 你需要改的地方：
 *   - "1"改成你的波形组件ID
 *     例如你在USART HMI里波形控件ID是2，就改成 add 2,0,%d
 *   - "0"是通道号，如果你只需要显示1条曲线，保持为0
 *     如果要显示多条（如3轴加速度），改成 0/1/2
 */
static void UI_UpdateWave(const UI_Data_t *d)
{
	char cmd[32];

	/* 构造波形指令 */
	/* 修改3: "1"改成你USART HMI里波形控件的ID */
	/* 修改4: 如需多个通道，可复制此行改成通道1、2... */
	snprintf(cmd, sizeof(cmd), "add 1,0,%d", d->waveSample);
	TJC_SendCommand(cmd);

	/* 多通道示例（如果你要显示3条曲线）：
	   char cmd2[32], cmd3[32];
	   snprintf(cmd2, sizeof(cmd2), "add 1,1,%d", d->waveSample2);
	   snprintf(cmd3, sizeof(cmd3), "add 1,2,%d", d->waveSample3);
	   TJC_SendCommand(cmd2);
	   TJC_SendCommand(cmd3);
	*/
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
	char text[32];

	/* 格式化温度文本 */
	snprintf(text, sizeof(text), "T=%.1fC", d->temp);
	/* 修改5: "t1"改成你USART HMI里参数页的温度文本组件名 */
	TJC_SetText("t1", text);

	/* 设置电压数值（浮点） */
	/* 修改6: "n1"改成你USART HMI里参数页的电压数字组件名 */
	TJC_SetFloat("n1", d->volt);

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
 *       d->volt = ADC_GetChannelValue(1);         // 从ADC通道1读电压
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
	d->volt = 12.0f + (float)(cnt % 30) * 0.01f;      /* 电压在12~12.3V之间循环 */
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
	uint8_t keyNum;

	/* 第1步：硬件初始化 */
	OLED_Init();                       /* 初始化OLED小屏（调试显示用） */
	Key_Init();                        /* 初始化板载按键（PB1/PB11） */
	Serial_Init(9600);                 /* 初始化USART1，波特率9600（必须和屏幕一致） */

	/* 显示启动信息 */
	OLED_ShowString(1, 1, "TJC UI FRAME");
	Delay_ms(3000);                    /* 必须等待屏幕启动完毕 */

	/* 第2步：初始化UI状态 */
	UI_SetPage(PAGE_HOME);             /* 切到首页 */

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

		/* 采集业务数据（当前用模拟，实际项目改成真实采样） */
		Data_Simulate(&g_Data);

		/* 根据当前页刷新对应的屏幕控件 */
		UI_Process(&g_Data);

		/* 显示当前页号在OLED上（调试用） */
		OLED_ShowNum(2, 1, (uint32_t)g_Page, 1);

		/* 延时100ms再处理下一次 */
		Delay_ms(100);
	}
}
