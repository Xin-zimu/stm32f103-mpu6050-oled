# MPU6050 3D Attitude Viewer

电脑端上位机，用于接收 STM32 串口输出的姿态帧，并用 3D 长方体实时显示 Roll / Pitch / Yaw。

## STM32 串口协议

固件每 50ms 输出一帧：

```text
ATT,roll,pitch,yaw
```

示例：

```text
ATT,12.30,-3.20,45.80
```

## 运行

安装依赖：

```powershell
py -m pip install -r mpu_viewer\requirements.txt
```

连接 STM32。你的串口号是 `COM9`：

```powershell
py mpu_viewer\main.py --port COM9
```

没有硬件时先看演示：

```powershell
py mpu_viewer\main.py --demo
```

程序会打开本地页面。页面中的 3D 长方体会跟随姿态角旋转。

停止上位机：回到 PowerShell 窗口，按 `Ctrl+C`。

## 参数

- `--port COM3`：串口号。
- `--baud 115200`：波特率，默认 115200。
- `--demo`：使用模拟数据，不打开串口。
- `--http-port 8765`：本地页面端口。

## 注意

MPU6050 没有磁力计，Yaw 会随时间漂移；第一版主要看 Roll 和 Pitch。
