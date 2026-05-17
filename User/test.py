import serial
import time

# ==========================================
# 🔌 端口配置区
# ==========================================
PORT_PYNQ   = "COM3"   # 连着 PYNQ 开发板的端口 (请确认是不是 COM3)
PORT_SCREEN = "COM10"  # 🌟 已修改: 连着 CH340 (串口屏) 的端口
BAUD_RATE   = 115200

print(f">>> 正在初始化电脑中转站...")

try:
    # 打开两个串口
    pynq_uart = serial.Serial(PORT_PYNQ, BAUD_RATE, timeout=0.1)
    screen_uart = serial.Serial(PORT_SCREEN, BAUD_RATE, timeout=0.1)
    
    print(f">>> 🚀 转发通道已建立！")
    print(f"      [{PORT_PYNQ} (接收 Zynq 数据)] ---> [{PORT_SCREEN} (发送给串口屏)]")
    print(">>> 等待接收数据 (按 Ctrl+C 停止)...\n" + "-"*50)

    while True:
        # 如果从 PYNQ 收到了数据
        if pynq_uart.in_waiting > 0:
            # 1. 把所有积压的字节数据一次性读出来
            raw_data = pynq_uart.read(pynq_uart.in_waiting)
            
            # 2. 原封不动、一滴不漏地砸进串口屏的通道里
            screen_uart.write(raw_data)
            
            # 3. 顺便在电脑终端打印出来，方便你看着它工作
            try:
                # 尝试解码成文本显示，滤除掉无法解码的乱码
                text_data = raw_data.decode('utf-8', errors='ignore').strip()
                if text_data:
                    print(f"🚚 已转发: {text_data}")
            except:
                pass
                
        # 睡一小会儿，防止把电脑 CPU 占满
        time.sleep(0.01)

except serial.SerialException as e:
    print(f"\n❌ 串口打开失败！")
    print(f"请检查: 1. COM口写对了吗？ 2. 是不是开了其他串口助手把端口占用了？")
    print(f"报错详情: {e}")
except KeyboardInterrupt:
    print("\n>>> 🛑 中转站已安全关闭。")
finally:
    # 退出时释放资源
    if 'pynq_uart' in locals() and pynq_uart.is_open:
        pynq_uart.close()
    if 'screen_uart' in locals() and screen_uart.is_open:
        screen_uart.close()