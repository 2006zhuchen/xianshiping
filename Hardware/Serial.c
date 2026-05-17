/*
 *  Serial.c  —— 串口驱动 + TJC 串口屏指令实现
 *
 *  这个文件是串口通信的"发动机"，里面包含了：
 *    1. 基础串口驱动（初始化、发送字节、发送字符串）
 *    2. TJC 串口屏的专用控制指令
 *    3. 表格显示工具
 *
 *  初学者只需要看"怎么调用"，不需要修改这个文件的内容。
 *  所有函数的使用方法见 Seruak.h 的注释。
 *
 *  硬件连接：
 *    STM32 PA9  (USART1_TX)  ←→  屏幕 RX（屏幕的"耳朵"）
 *    STM32 PA10 (USART1_RX)  ←→  屏幕 TX（屏幕的"嘴"，可不接）
 *    STM32 GND               ←→  屏幕 GND（必须接，否则信号回不来！！）
 *    屏幕 VCC                 ←→  5V 独立电源（不要从 STM32 取电）
 */

#include "stm32f10x.h"   /* STM32 标准外设库，提供 GPIO、USART 等函数 */
#include "Seruak.h"       /* 自己的头文件 */
#include "Delay.h"        /* 延时函数（Delay_ms 等） */
#include <stdarg.h>       /* C 标准库：可变参数（...）的支持 */
#include <stdio.h>        /* C 标准库：snprintf、vsnprintf 等格式化函数 */


/* ================================================================
 *  第一部分：基础串口驱动
 *
 *  串口（USART）是什么？
 *    串口是一种通信方式，用两根线（TX 发送、RX 接收）在设备之间传数据。
 *    就像两个人打电话：你说（TX）我听（RX），我说（RX）你听（TX）。
 *
 *  波特率是什么？
 *    通信速度。9600 表示每秒传 9600 个比特（约 960 个字符/秒）。
 *    双方必须约定相同的速度，否则收到的是乱码。
 *    TJC 屏幕出厂默认 9600。
 * ================================================================ */

/*
 *  函数：Serial_Init()
 *  作用：初始化 STM32 的串口 1，配置好引脚和通信参数
 *  参数：baudrate = 波特率，填 9600 或 115200
 *
 *  这个函数做了什么？
 *    第 1 步：打开 GPIOA 和 USART1 的时钟（给硬件供电）
 *    第 2 步：把 PA9 配置成"发送模式"（复用推挽输出）
 *    第 3 步：把 PA10 配置成"接收模式"（浮空输入）
 *    第 4 步：设置串口的通信参数（波特率、数据位、停止位、校验位）
 *    第 5 步：启动串口
 */
void Serial_Init(uint32_t baudrate)
{
	/* ---- 定义两个结构体变量，用来填写配置参数 ---- */
	GPIO_InitTypeDef  GPIO_InitStructure;   /* GPIO 引脚配置结构体 */
	USART_InitTypeDef USART_InitStructure;  /* 串口通信配置结构体 */
	NVIC_InitTypeDef  NVIC_InitStructure;   /* 串口中断配置结构体 */

	/*
	 * 第 1 步：打开时钟
	 * 为什么要开时钟？
	 *   STM32 为了省电，每个外设（GPIO、USART 等）默认是断电状态。
	 *   使用之前必须先"开时钟"，等于给这个外设接通电源。
	 */
	/* RCC_APB2PeriphClockCmd = 打开 APB2 总线上的外设时钟 */
	/* USART1 和 GPIOA 都在 APB2 总线上，所以一起开 */
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);

	/*
	 * 第 2 步：配置 PA9（USART1 的 TX 发送脚）
	 *   GPIO_Mode_AF_PP = 复用推挽输出
	 *     "复用"  = 这个脚不当作普通 GPIO 用，而是给 USART 外设用
	 *     "推挽"  = 能输出高电平和低电平（3.3V / 0V）
	 *     "输出"  = 这个脚是往外送数据的
	 */
	GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_9;               /* 选中 PA9 */
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;         /* 信号速度 50MHz */
	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;          /* 复用推挽输出 */
	GPIO_Init(GPIOA, &GPIO_InitStructure);                     /* 把配置写入 GPIOA 硬件 */

	/*
	 * 第 3 步：配置 PA10（USART1 的 RX 接收脚）
	 *   GPIO_Mode_IN_FLOATING = 浮空输入
	 *     "输入"    = 这个脚是接收数据的
	 *     "浮空"    = 不接内部上拉/下拉电阻，靠外部信号决定电平
	 */
	GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_10;               /* 选中 PA10 */
	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IN_FLOATING;     /* 浮空输入模式 */
	GPIO_Init(GPIOA, &GPIO_InitStructure);                     /* 把配置写入 GPIOA 硬件 */

	/*
	 * 第 4 步：设置串口通信参数
	 *   - 波特率：    由调用者传入，通常是 9600
	 *   - 数据位：    8 位（一次发 8 个比特 = 1 个字节）
	 *   - 停止位：    1 位（发完一个字节后停顿多久）
	 *   - 校验位：    无校验（不检查传输错误，简单够用）
	 *   - 硬件流控：  关闭（不用额外的流控线，只靠 TX/RX 两根线）
	 *   - 模式：      同时启用发送和接收
	 */
	USART_InitStructure.USART_BaudRate            = baudrate;  /* 波特率 */
	USART_InitStructure.USART_WordLength          = USART_WordLength_8b;  /* 8 位 */
	USART_InitStructure.USART_StopBits            = USART_StopBits_1;     /* 1 位停止 */
	USART_InitStructure.USART_Parity              = USART_Parity_No;      /* 无校验 */
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None; /* 无流控 */
	USART_InitStructure.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;   /* 收发都开 */
	USART_Init(USART1, &USART_InitStructure);      /* 把配置写入 USART1 硬件 */

	/*
	 * 第 5 步：启动串口 1
	 *   配置完之后 USART1 还在待机，需要显式"打开开关"
	 */
	USART_Cmd(USART1, ENABLE);

	/*
	 * 第 6 步：开启 USART1 接收中断（RXNE）
	 *   这样屏幕返回的数据（例如 sendme 的 0x66,page,FF,FF,FF）
	 *   就会进入 USART1_IRQHandler，再交给上层解析。
	 */
	USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);

	NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);
}

/*
 *  函数：Serial_SendByte()
 *  作用：发送一个字节（8 位二进制数据）
 *  参数：byte = 要发的字节，0~255
 *
 *  原理：
 *    先检查"发送缓冲区是否为空"（TXE 标志位），
 *    如果上一个字节还没发完，就等着（while 循环死等），
 *    发完了就把新数据塞进发送寄存器。
 */
void Serial_SendByte(uint8_t byte)
{
	/*
	 * USART_GetFlagStatus() = 查询串口状态标志
	 * USART_FLAG_TXE = 发送数据寄存器空标志
	 *   TXE = 1（SET）   → 缓冲区空，可以发下一个字节
	 *   TXE = 0（RESET） → 缓冲区还在忙，需要等待
	 *
	 * while(... == RESET);  → 只要没空，就一直等
	 */
	while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);

	/*
	 * USART_SendData() = 把数据写入发送寄存器
	 * 硬件会自动把这个字节一位一位地通过 PA9 脚发出去
	 */
	USART_SendData(USART1, byte);
}

/*
 *  函数：Serial_SendString()
 *  作用：发送一个字符串（逐个字符发）
 *  参数：str = 要发送的字符串（必须以 '\0' 结尾）
 *
 *  原理：
 *    字符串在 C 语言里就是一个字符数组，末尾用 '\0'（值为 0 的字节）标记结束。
 *    这个函数用一个 while 循环，逐个字符调用 Serial_SendByte，
 *    遇到 '\0' 就停止。
 */
void Serial_SendString(char *str)
{
	/*
	 * *str 拿到的是当前字符
	 * 在 C 语言中，'\0' 的 ASCII 值是 0，即"假"
	 * while (*str) 等价于 while (*str != '\0')
	 */
	while (*str)
	{
		Serial_SendByte(*str);   /* 发送当前字符 */
		str++;                    /* 指针后移，指向下一个字符 */
	}
}

/*
 *  函数：Serial_Printf()
 *  作用：格式化输出，用法和 printf 完全一样
 *  参数：format = 格式字符串，后面可以跟 N 个参数
 *
 *  示例：
 *    Serial_Printf("温度=%d，湿度=%d%%\r\n", temp, humi);
 *    屏幕收到: "温度=26，湿度=65%\r\n"
 *
 *  格式说明：
 *    %d  → 整数（十进制）
 *    %f  → 浮点数
 *    %s  → 字符串
 *    %c  → 单个字符
 *    %%  → 百分号本身
 *    \r\n → 回车换行（屏幕上的"敲回车"）
 */
void Serial_Printf(char *format, ...)
{
	char buf[128];              /* 临时缓冲区，存放格式化后的字符串 */
	va_list arg;                /* va_list 用来处理 ... 可变参数 */

	va_start(arg, format);      /* 根据 format 定位可变参数的起始地址 */
	vsnprintf(buf, sizeof(buf), format, arg);  /* 把格式化结果写入 buf */
	va_end(arg);                /* 清理 */

	Serial_SendString(buf);     /* 把格式化好的字符串发出去 */
}


/* ================================================================
 *  第二部分：TJC 串口屏指令
 *
 *  TJC 屏幕的通信协议：
 *    每条指令的格式： "命令内容" + 0xFF 0xFF 0xFF（三个结束符字节）
 *
 *    屏幕收到指令后，会解析并执行。
 *    例如：
 *      发送: t0.txt="Hello"FF FF FF
 *      屏幕: 把 t0 组件的文字设为 "Hello"
 *
 * 重要注意事项：
 *    1. 屏幕上电后需要 2~3 秒才能接收指令（期间屏幕在初始化）
 *    2. 每条指令之间建议间隔 5~10ms，否则可能丢指令
 *    3. 组件名（t0、n0 等）必须和 USART HMI 软件里设置的完全一致
 * ================================================================ */

/*
 *  函数：TJC_SendCommand()
 *  作用：发送一条 TJC 指令，自动追加三个 0xFF 结束符
 *  参数：cmd = 指令字符串（不含结束符）
 *
 *  这是所有 TJC 函数的核心。
 *  其他函数（SetText、SetNum 等）都是先拼出指令字符串，
 *  然后调用这个函数发送。
 *
 *  示例：
 *    TJC_SendCommand("page 0");
 *    → 实际发出: page 0 + FF FF FF
 */
void TJC_SendCommand(char *cmd)
{
	Serial_SendString(cmd);      /* 先发指令文字 */
	Serial_SendByte(0xFF);       /* 再发结束符 */
	Serial_SendByte(0xFF);       /* 再发结束符 */
	Serial_SendByte(0xFF);       /* 再发结束符 */
	/*
	 * 为什么是三个 0xFF？
	 *   这是 TJC 屏幕规定的"指令结束标记"。
	 *   屏幕收到三个连续的 0xFF，就知道一条指令发完了，开始执行。
	 */
}

/*
 *  函数：TJC_SetPage()
 *  作用：切换屏幕显示的页面
 *  参数：page = 页面编号（0 是第一页）
 *
 *  关于页面：
 *    在 USART HMI 软件里，你可以在左上角添加多个页面（page0, page1...）
 *    每个页面可以放不同的组件（文本、按钮、数字等）
 *    这个函数让屏幕跳到指定页面
 *
 *  注意：切换页面需要时间让屏幕重新绘制，
 *        所以这个函数会额外等待 50ms
 */
void TJC_SetPage(uint8_t page)
{
	char cmd[16];   /* 存放拼好的指令字符串 */

	/* snprintf = 把格式化后的文字写入 cmd
	   例如 page=0 时，cmd = "page 0" */
	snprintf(cmd, sizeof(cmd), "page %d", page);

	TJC_SendCommand(cmd);     /* 发送指令 */

	Delay_ms(50);             /* 等 50ms 让屏幕完成页面切换 */
}

/*
 *  函数：TJC_ClearPage()
 *  作用：清除指定页面的内容
 *  参数：page = 要清除的页面编号
 *
 *  注意：这个函数清除的是页面上所有组件的内容，
 *        不是删除组件本身
 */
void TJC_ClearPage(uint8_t page)
{
	char cmd[16];
	snprintf(cmd, sizeof(cmd), "cls %d", page);
	TJC_SendCommand(cmd);
	Delay_ms(10);             /* 等 10ms */
}

/*
 *  函数：TJC_SetText()
 *  作用：设置文本组件（t0, t1...）的显示内容
 *  参数：comp = 组件名，例如 "t0"
 *        text = 要显示的文字
 *
 *  示例调用：
 *    TJC_SetText("t0", "Hello");
 *    → 拼接出指令: t0.txt="Hello"
 *    → 加上结束符发出: t0.txt="Hello" + FF FF FF
 *
 *  注意：
 *    text 内部不要包含双引号 "，否则会打乱指令格式
 *    t0 组件里的文字支持 \r\n 换行（之前说过的表格就是利用这个）
 */
void TJC_SetText(char *comp, char *text)
{
	char cmd[256];   /* 增大缓冲区，避免表格等长文本被截断 */

	/*
	 * snprintf 格式化：
	 *   %s  = 字符串占位符
	 *   第一个 %s 被 comp  替换 → 如 "t0"
	 *   第二个 %s 被 text  替换 → 如 "Hello"
	 *   \"   = 转义后的双引号（因为 C 语言中 " 是特殊字符）
	 *
	 * 结果 cmd = t0.txt="Hello"
	 */
	snprintf(cmd, sizeof(cmd), "%s.txt=\"%s\"", comp, text);

	TJC_SendCommand(cmd);     /* 发送 */
	Delay_ms(5);              /* 每条指令之间留 5ms 间隔 */
}

/*
 *  函数：TJC_SetNum()
 *  作用：设置数字组件（n0, n1...）的值（整数）
 *  参数：comp = 组件名，例如 "n0"
 *        val  = 要显示的数字，例如 12345
 *
 *  示例：
 *    TJC_SetNum("n0", 12345);
 *    → 指令: n0.val=12345 + FF FF FF
 *
 *  %d 会把 val 格式化为十进制数字
 */
void TJC_SetNum(char *comp, int32_t val)
{
	char cmd[64];
	snprintf(cmd, sizeof(cmd), "%s.val=%d", comp, (int)val);
	TJC_SendCommand(cmd);
	Delay_ms(5);
}

/*
 *  函数：TJC_SetFloat()
 *  作用：设置数字组件的值（小数）
 *  参数：comp = 组件名
 *        val  = 要显示的浮点数
 *
 *  实现说明：
 *    TJC 的数字组件本身不支持 .val=3.14 这种写法，
 *    所以这里把小数乘以 100 变成整数再发送。
 *    例如 26.8 → 26.8 × 100 = 2680 → 发送 n0.val=2680
 *
 *    然后在 USART HMI 的数字组件属性里，
 *    设置小数点位置为 2 位，屏幕就会自动显示成 26.80
 *
 *  局限性：目前只支持最简的转换（四舍五入到整数）
 *    如需精确小数，可用 snprintf 直接把浮点转成字符串格式
 */
void TJC_SetFloat(char *comp, float val)
{
	char cmd[64];
	/*
	 * (int)(val * 100 + 0.5)
	 *   乘 100 把小数放大
	 *   + 0.5 是四舍五入
	 *   (int) 强制转成整数
	 *
	 *   例如: val = 26.856
	 *     26.856 × 100 = 2685.6
	 *     2685.6 + 0.5  = 2686.1
	 *     (int)2686.1   = 2686
	 */
	snprintf(cmd, sizeof(cmd), "%s.val=%d", comp, (int)(val * 100 + 0.5));
	TJC_SendCommand(cmd);
	Delay_ms(5);
}

/*
 *  函数：TJC_SetAttr()
 *  作用：修改组件的任意属性
 *  参数：comp = 组件名（"t0", "n0", "b0" 等）
 *        attr = 属性名，例如 "txt"（文字）、"pco"（字体色）、"bco"（背景色）
 *        val  = 属性值（字符串形式）
 *
 *  常用属性对照表：
 *    ┌────────┬────────────────────┬──────────────────────┐
 *    │ 属性名  │ 含义               │ 示例值               │
 *    ├────────┼────────────────────┼──────────────────────┤
 *    │ "txt"  │ 文字内容           │ "Hello"              │
 *    │ "val"  │ 数值               │ "123"                │
 *    │ "bco"  │ 背景颜色           │ "RED", "BLUE", 65535 │
 *    │ "pco"  │ 字体颜色           │ "GREEN", "WHITE"     │
 *    │ "font" │ 字体 ID            │ "0", "1"             │
 *    │ "pic"  │ 图片 ID            │ "0", "1"             │
 *    │ "x"    │ X 坐标（像素）     │ "100"                │
 *    │ "y"    │ Y 坐标（像素）     │ "200"                │
 *    │ "w"    │ 宽度（像素）       │ "300"                │
 *    │ "h"    │ 高度（像素）       │ "200"                │
 *    └────────┴────────────────────┴──────────────────────┘
 *
 *  示例：
 *    TJC_SetAttr("t0", "pco", "RED");   // t0 字体变成红色
 *    TJC_SetAttr("n0", "val", "999");    // n0 值变成 999（效果等同于 TJC_SetNum）
 *    TJC_SetAttr("b0", "txt", "启动");   // 按钮 b0 上的字改成"启动"
 */
void TJC_SetAttr(char *comp, char *attr, char *val)
{
	char cmd[128];
	/* 拼接格式：组件名.属性名=属性值
	   例如: t0.pco=RED */
	snprintf(cmd, sizeof(cmd), "%s.%s=%s", comp, attr, val);
	TJC_SendCommand(cmd);
	Delay_ms(5);
}

/*
 *  函数：TJC_Click()
 *  作用：模拟手指点击屏幕上的按钮
 *  参数：comp  = 按钮组件名，例如 "b0"
 *        press = 1 按下按钮 / 0 松开按钮
 *
 *  用途：STM32 可以代替人来"操作"屏幕上的按钮，
 *        触发按钮的回调事件，执行预设逻辑
 *
 *  示例（模拟一次完整点击）：
 *    TJC_Click("b0", 1);   // 按下
 *    Delay_ms(100);        // 按住 100ms
 *    TJC_Click("b0", 0);   // 松开
 */
void TJC_Click(char *comp, uint8_t press)
{
	char cmd[32];
	snprintf(cmd, sizeof(cmd), "click %s,%d", comp, press);
	TJC_SendCommand(cmd);
	Delay_ms(5);
}

/*
 *  函数：TJC_GetVal()
 *  作用：向屏幕请求某个组件的当前值
 *  参数：comp = 组件名
 *
 *  注意：
 *    这个函数只发送"请求"指令，
 *    屏幕收到后会从 TX 脚返回数据，
 *    STM32 需要在 PA10 上接收并解析返回的数据。
 *    目前代码还没实现接收部分，后续可以补上。
 *
 *  示例：
 *    TJC_GetVal("t0");   // 请求 t0 的文字内容
 *    屏幕返回类似: 0xFF 0xFF 0xFF + 数据...
 */
void TJC_GetVal(char *comp)
{
	char cmd[32];
	snprintf(cmd, sizeof(cmd), "get %s.txt", comp);
	TJC_SendCommand(cmd);
	Delay_ms(5);
}

/*
 *  函数：TJC_SetBaud()
 *  作用：修改屏幕的波特率
 *  参数：baud = 新的波特率数值，例如 115200
 *
 *  警告：
 *    这个函数发送后，屏幕会立刻切换到新的波特率。
 *    你的 STM32 也必须同步调用 Serial_Init(新波特率)
 *    否则两边速度不同，所有通信都会变成乱码！
 *
 *  使用步骤：
 *    1. TJC_SetBaud(115200);    // 告诉屏幕：以后用 115200 说话
 *    2. Serial_Init(115200);    // STM32 也切到 115200
 *
 *  如果忘记第 2 步，串口屏就"失联"了。
 *  想恢复的话，需要给屏幕重新用 9600 下载 USART HMI 工程。
 */
void TJC_SetBaud(uint32_t baud)
{
	char cmd[32];
	snprintf(cmd, sizeof(cmd), "baud=%d", (int)baud);
	TJC_SendCommand(cmd);
	Delay_ms(10);
}

/*
 *  函数：TJC_Reset()
 *  作用：复位屏幕，等于给屏幕断电再上电
 *
 *  发送后屏幕会重启，大约需要 2~3 秒才能重新接收指令。
 *  这个函数内部等了 2000ms（2 秒），调用后不用再额外等待。
 */
void TJC_Reset(void)
{
	TJC_SendCommand("rest");
	Delay_ms(2000);   /* 屏幕重启需要 2 秒 */
}


/* ================================================================
 *  第三部分：表格显示工具
 *
 *  这些函数帮助你用"拼接大字符串"的方式在屏幕上显示表格。
 *
 *  为什么不用多个组件排成表格？
 *    如果表格有几十个格子，在 USART HMI 里拖几十个组件太累了。
 *    拼字符串的方式只需要 1 个文本组件，代码里灵活拼表。
 *
 *  关键知识点：
 *    1. \r\n = 回车换行（在屏幕上另起一行）
 *    2. 空格 = 在屏幕上留空白
 *    3. 每列用固定宽度 + 空格补全 = 列对齐
 *    4. 等宽字体 = 每个字占的宽度一样，列才能严格对齐
 * ================================================================ */

/*
 *  函数：TJC_Table_Send()
 *  作用：把拼好的表格字符串发送到指定文本组件
 *  参数：comp     = 文本组件名（如 "t0"）
 *        tableStr = 整张表格的字符串
 *
 *  这个函数就是 TJC_SetText 的别名，
 *  只是从名字上能看出它是用来发表格的。
 */
void TJC_Table_Send(char *comp, char *tableStr)
{
	TJC_SetText(comp, tableStr);
}

/*
 *  函数：TJC_Table_FormatCell()
 *  作用：把一段文字变成"固定宽度"的单元格（不够就补空格）
 *
 *  参数：dst   = 输出缓冲区（一个 char 数组，用来放结果）
 *        src   = 原始文字
 *        width = 想要的总宽度（字符数）
 *
 *  示例：
 *    输入 src="温度", width=8
 *    "温度" 是 2 个字符，需要补 6 个空格
 *    输出 dst = "温度      "
 *
 *    输入 src="25.6", width=8
 *    "25.6" 是 4 个字符，需要补 4 个空格
 *    输出 dst = "25.6    "
 *
 *  逐行解释：
 */
void TJC_Table_FormatCell(char *dst, char *src, uint8_t width)
{
	uint8_t i = 0;   /* i = 当前写入 dst 的位置 */

	/*
	 * 第 1 步：逐个复制 src 的字符到 dst
	 *   条件: 还没到 src 末尾（*src != '\0'）且还没填满 width 个位置
	 */
	while (*src && i < width)
	{
		dst[i] = *src;   /* 复制一个字符 */
		i++;             /* 写入位置后移 */
		src++;           /* 源位置后移 */
	}

	/*
	 * 第 2 步：剩余位置全部填空格
	 *   如果 src 的字符数 < width，循环继续
	 *   如果 src 的字符数 >= width，i=width，循环不执行
	 */
	while (i < width)
	{
		dst[i] = ' ';    /* 填入空格 */
		i++;
	}

	dst[i] = '\0';       /* 字符串必须以 '\0' 结尾 */
}

/*
 *  函数：TJC_Table_FormatNum()
 *  作用：把数字转成字符串，然后补空格对齐
 *
 *  参数：dst   = 输出缓冲区
 *        num   = 要显示的数字（整数）
 *        width = 单元格宽度
 *
 *  示例：
 *    输入 num=256, width=8
 *    "256" 是 3 个字符，补 5 个空格
 *    输出 dst = "256     "
 *
 *  逐行解释：
 */
void TJC_Table_FormatNum(char *dst, int32_t num, uint8_t width)
{
	char buf[16];       /* 临时存放数字转成的字符串 */
	uint8_t i = 0;       /* buf 的写入位置 */
	uint8_t j = 0;       /* dst 的复制位置 */

	/*
	 * 第 1 步：处理负数
	 */
	if (num < 0)
	{
		buf[i] = '-';    /* 先放一个负号 */
		i++;
		num = -num;      /* 把数字变成正数，方便后面处理 */
	}

	/*
	 * 第 2 步：把数字的每一位提取出来
	 *
	 *   算法：反复对 10 取余数（%10），得到个位数字，
	 *   再除以 10（/10）去掉个位，直到数字为 0。
	 *
	 *   例如 num = 256:
	 *     第 1 次: 256 % 10 = 6  → tmp[0]='6',   num=25
	 *     第 2 次: 25  % 10 = 5  → tmp[1]='5',   num=2
	 *     第 3 次: 2   % 10 = 2  → tmp[2]='2',   num=0 停止
	 *     tmp = ['6','5','2']
	 *     反转后 buf = ['2','5','6','\0']  → "256"
	 */
	{
		char tmp[12];    /* 临时存放反转的数字 */
		uint8_t k = 0;

		do
		{
			tmp[k] = (num % 10) + '0';   /* 取个位，加 '0' 转成 ASCII 字符 */
			k++;
			num = num / 10;               /* 去掉个位 */
		} while (num > 0);               /* 直到数字为 0 */

		/* 数字是反的（个位在前），需要反转复制到 buf */
		while (k > 0)
		{
			buf[i] = tmp[k - 1];         /* 从后往前取 */
			i++;
			k--;
		}
	}

	/*
	 * 第 3 步：把 buf 的内容复制到 dst
	 */
	while (j < i && j < width)
	{
		dst[j] = buf[j];
		j++;
	}

	/*
	 * 第 4 步：剩余位置填空格
	 */
	while (j < width)
	{
		dst[j] = ' ';
		j++;
	}

	dst[j] = '\0';       /* 字符串结束符 */
}

/*
 * TJC_Table_FormatFreq5() — 频率 5 位零填充
 *   例: freq=100   dst 为 "00100" + 右侧空格补到 width
 *       freq=37272 dst 为 "37272" + 右侧空格补到 width
 */
void TJC_Table_FormatFreq5(char *dst, uint32_t freq, uint8_t width)
{
	char buf[8];
	uint8_t i, j;

	/* 数字转字符到 buf 末尾 */
	buf[5] = '\0';
	for (i = 5; i > 0; i--)
	{
		buf[i - 1] = (char)((freq % 10u) + '0');
		freq /= 10u;
	}

	/* 复制到 dst */
	j = 0;
	while (j < 5 && j < width)
	{
		dst[j] = buf[j];
		j++;
	}
	/* 剩余填空格 */
	while (j < width)
	{
		dst[j] = ' ';
		j++;
	}
	dst[j] = '\0';
}

/*
 * TJC_Table_FormatVolt() — 电压 ×100 整数 → 2 位小数
 *   例: amp100=100 (1.00V) dst 为 "1.00" + 右侧空格补到 width
 *       amp100=3   (0.03V) dst 为 "0.03" + 右侧空格补到 width
 *       amp100=40  (0.40V) dst 为 "0.40" + 右侧空格补到 width
 */
void TJC_Table_FormatVolt(char *dst, uint16_t amp100, uint8_t width)
{
	uint8_t intPart, frac, len, j;
	char buf[8];

	intPart = (uint8_t)(amp100 / 100u);
	frac    = (uint8_t)(amp100 % 100u);

	/* 拼 "整数.小数" */
	len = 0;
	if (intPart >= 100)      { buf[len++] = (char)((intPart / 100) + '0'); }
	if (intPart >= 10 || len) { buf[len++] = (char)(((intPart / 10) % 10) + '0'); }
	buf[len++] = (char)((intPart % 10) + '0');
	buf[len++] = '.';
	buf[len++] = (char)((frac / 10) + '0');
	buf[len++] = (char)((frac % 10) + '0');

	/* 复制到 dst */
	j = 0;
	while (j < len && j < width)
	{
		dst[j] = buf[j];
		j++;
	}
	while (j < width)
	{
		dst[j] = ' ';
		j++;
	}
	dst[j] = '\0';
}
