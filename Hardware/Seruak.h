#ifndef __SERIAL_H
#define __SERIAL_H

/*
 *  Seruak.h  —— 串口屏驱动头文件
 *
 *  这个文件的作用：告诉编译器"有哪些函数可以用"。
 *  每个 .c 文件如果想用串口功能，只需要 #include "Seruak.h" 这一行。
 *
 *  你不需要修改这个文件，只需在 main.c 里调用下面的函数即可。
 */

#include "stm32f10x.h"   /* STM32 标准外设库，里面定义了 GPIO、USART 等寄存器 */

/* ================================================================
 *  第一部分：基础串口函数（底层的发送功能）
 * ================================================================ */

void Serial_Init(uint32_t baudrate);
/*
 *  初始化串口 1（USART1）
 *  参数：baudrate = 波特率，也就是通信速度
 *        屏幕和 STM32 必须设成相同的波特率才能通信
 *        常用值：9600（出厂默认） 或 115200
 *  示例：Serial_Init(9600);
 */

void Serial_SendByte(uint8_t byte);
/*
 *  发送一个字节（8 位数据）
 *  参数：byte = 要发送的字节，范围 0~255
 *  示例：Serial_SendByte(0xFF);  // 发送十六进制 0xFF
 */

void Serial_SendString(char *str);
/*
 *  发送一个字符串（一串字符）
 *  参数：str = 以 '\0' 结尾的字符串
 *  示例：Serial_SendString("Hello");  // 屏幕收到 Hello
 */

void Serial_Printf(char *format, ...);
/*
 *  格式化发送（用法和 C 语言的 printf 一样）
 *  参数：format = 格式字符串，... = 可变参数
 *  示例：Serial_Printf("温度=%d\r\n", temp);  // 屏幕收到 "温度=26"
 */

/* ================================================================
 *  第二部分：TJC 串口屏专用指令
 *
 *  TJC 屏幕的通信协议：
 *    每条指令 = "文字命令" + 三个 0xFF（结束符）
 *    例如：t0.txt="Hello" + FF FF FF
 *
 *  组件命名规则（在 USART HMI 软件里设置）：
 *    t0, t1, t2...  → 文本组件（显示文字）
 *    n0, n1, n2...  → 数字组件（显示数字）
 *    b0, b1, b2...  → 按钮组件（接收点击）
 * ================================================================ */

void TJC_SendCommand(char *cmd);
/*
 *  向屏幕发送一条 TJC 指令（自动追加三个 0xFF 结束符）
 *  这是所有 TJC 函数的底层，其他函数最终都调用它
 *  参数：cmd = 指令字符串，例如 "page 0"、"t0.txt=\"Hello\""
 *  示例：TJC_SendCommand("cls 0");  // 清屏
 */

void TJC_SetPage(uint8_t page);
/*
 *  切换屏幕显示的页面
 *  参数：page = 页面编号（从 0 开始）
 *        在 USART HMI 里你可以设计多个页面（page 0, page 1...）
 *  示例：TJC_SetPage(0);  // 跳到第 0 页
 */

void TJC_ClearPage(uint8_t page);
/*
 *  清除指定页面上的所有内容
 *  参数：page = 要清除的页面编号
 *  示例：TJC_ClearPage(0);  // 把第 0 页清空
 */

void TJC_SetText(char *comp, char *text);
/*
 *  设置文本组件的显示内容
 *  参数：comp = 组件名，例如 "t0"、"t1"
 *        text = 要显示的文字
 *  示例：TJC_SetText("t0", "你好 STM32");
 *        等效于屏幕收到: t0.txt="你好 STM32" + FF FF FF
 */

void TJC_SetNum(char *comp, int32_t val);
/*
 *  设置数字组件的值（整数）
 *  参数：comp = 组件名，例如 "n0"、"n1"
 *        val  = 要显示的数字，例如 12345
 *  示例：TJC_SetNum("n0", 12345);
 *        等效于屏幕收到: n0.val=12345 + FF FF FF
 */

void TJC_SetFloat(char *comp, float val);
/*
 *  设置数字组件的值（小数 / 浮点数）
 *  参数：comp = 组件名，例如 "n0"、"n1"
 *        val  = 要显示的浮点数，例如 26.5
 *  注意：内部会把小数乘以 100 转成整数发送
 *        屏幕端需要在数字组件的属性里设小数点位数
 *  示例：TJC_SetFloat("n0", 26.5);
 */

void TJC_SetAttr(char *comp, char *attr, char *val);
/*
 *  修改组件的任意属性（颜色、字体、文字等）
 *  参数：comp = 组件名（"t0"、"b0"、"n0" 等）
 *        attr = 属性名，常用属性如下：
 *               "txt"  → 文字内容
 *               "val"  → 数值
 *               "bco"  → 背景颜色
 *               "pco"  → 字体颜色
 *               "pic"  → 图片编号
 *        val  = 属性值（字符串形式）
 *  示例：TJC_SetAttr("t0", "pco", "RED");   // t0 字体变红色
 *        TJC_SetAttr("t0", "bco", "BLUE");  // t0 背景变蓝色
 *        TJC_SetAttr("b0", "txt", "开始");  // b0 按钮文字改为"开始"
 */

void TJC_Click(char *comp, uint8_t press);
/*
 *  模拟点击按钮（让 STM32 代替人手去"按"屏幕上的按钮）
 *  参数：comp  = 按钮组件名，例如 "b0"
 *        press = 1 表示按下，0 表示松开
 *  示例：TJC_Click("b0", 1);  // 按下按钮 b0
 *        Delay_ms(100);
 *        TJC_Click("b0", 0);  // 松开按钮 b0
 */

void TJC_GetVal(char *comp);
/*
 *  向屏幕请求组件当前的值（屏幕会从 TX 脚返回数据）
 *  参数：comp = 组件名
 *  注意：需要读取 STM32 的 RX（PA10）来接收屏幕返回的数据
 *        这个函数只发送请求，接收需要另外写代码处理
 *  示例：TJC_GetVal("t0");  // 请求 t0 当前文字
 */

void TJC_SetBaud(uint32_t baud);
/*
 *  修改屏幕的通信波特率
 *  参数：baud = 新波特率，例如 115200
 *  注意：发送后屏幕立即切换到新波特率，
 *        STM32 也需要同步调用 Serial_Init(新波特率) 才能继续通信
 *  示例：TJC_SetBaud(115200);  // 屏幕切到 115200
 *        Serial_Init(115200);  // STM32 也切到 115200
 */

void TJC_Reset(void);
/*
 *  复位屏幕（等于给屏幕断电再上电）
 *  发送后屏幕会重启，大约需要 2~3 秒
 */

/* ================================================================
 *  第三部分：表格显示工具
 *
 *  原理：把一整张表拼成一个大字符串，
 *        用 \r\n 换行、用空格对齐列，
 *        一次性发给屏幕上的一个文本组件。
 *
 *  要在 USART HMI 里做的准备：
 *    拖一个文本组件到画布上，拉大，起名叫 t0，
 *    字体选等宽字体（这样每列才能对齐）。
 * ================================================================ */

void TJC_Table_Send(char *comp, char *tableStr);
/*
 *  发送一张完整的表格到大文本组件
 *  参数：comp     = 组件名（例如 "t0"）
 *        tableStr = 整张表格的字符串，行与行之间用 \r\n 分隔
 *  示例：
 *    TJC_Table_Send("t0",
 *        "温度   湿度\r\n"
 *        "25.6   68\r\n"
 *        "26.1   65");
 */

void TJC_Table_FormatCell(char *dst, char *src, uint8_t width);
/*
 *  格式化一个单元格：把文字复制出来，不够长就在后面补空格
 *  参数：dst   = 输出缓冲区（放格式化结果的地方）
 *        src   = 原始文字内容
 *        width = 单元格的固定宽度（不够就补空格，对齐用的）
 *  示例：
 *    char cell[10];
 *    TJC_Table_FormatCell(cell, "温度", 8);
 *    // cell 现在是 "温度      "（"温度" + 6 个空格，凑够 8 个字符）
 */

void TJC_Table_FormatNum(char *dst, int32_t num, uint8_t width);
/*
 *  格式化一个数字单元格：把数字变成文字再补空格对齐
 *  参数：dst   = 输出缓冲区
 *        num   = 要显示的数字
 *        width = 单元格宽度
 *  示例：
 *    char cell[10];
 *    TJC_Table_FormatNum(cell, 256, 8);
 *    // cell 现在是 "256     "
 */

void TJC_Table_FormatFreq5(char *dst, uint32_t freq, uint8_t width);
/*
 *  格式化频率（5 位零填充）：右侧补空格对齐到 width
 *  示例：freq=100   → "00100   " (width=8)
 *        freq=37272 → "37272   " (width=8)
 */

void TJC_Table_FormatVolt(char *dst, uint16_t amp100, uint8_t width);
/*
 *  格式化电压（×100 整数 → 2 位小数）：右侧补空格对齐到 width
 *  示例：amp100=100 (1.00V) → "1.00  " (width=6)
 *        amp100=3   (0.03V) → "0.03  " (width=6)
 */

#endif   /* __SERIAL_H  结束 */
