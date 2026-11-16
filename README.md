# STM32L452 MIDI 蜂鸣器播放器

基于 RT-Thread v5.3.0 的单片机 MIDI 播放玩具：把电脑上的 `.mid` 通过串口上传到板子片内 Flash，板子自己解析并用 Playtune 软件合成，经 TIM1 PWM 驱动蜂鸣器发声。换歌只需传文件，无需重新编译烧录。

## 特性

- 板载移植 miditones，直接在 MCU 上把 `.mid` 转成乐谱字节流
- Playtune 6 声部软件合成，TIM1 PWM 当 12-bit DAC（采样率 19531 Hz）
- FAL + littlefs 把片内 Flash 当文件系统（192 KB）存歌
- YMODEM 串口上传，音量软件可调且掉电保存

## 硬件

| 项目 | 说明 |
|---|---|
| 开发板 | STM32L452 Nucleo |
| 串口 | USB ST-LINK 虚拟串口，115200 |
| 音频输出 | PA8（TIM1_CH1）接无源蜂鸣器或小扬声器，另一端接 GND |
| 烧录 | 把 `rtthread.bin` 拷进 ST-LINK 虚拟 U 盘 |

## 快速上手

1. 烧录固件，打开串口进入 msh。
2. 上传歌曲（脚本把文件固定存为 `/music.mid`）：
   ```bash
   python tools/ymodem_send.py your.mid
   ```
3. 播放：
   ```text
   msh /> midiplay
   ```

## 常用命令

| 命令 | 作用 |
|---|---|
| `midiplay [路径]` | 解析并播放 `.mid`，默认 `/music.mid`，播完循环 |
| `builtin` | 播放内置示例，快速验证音频链路 |
| `volume [0..100]` | 查询 / 设置音量（自动保存到 `/vol.cfg`） |
| `audiostat` | 查看峰值与削波统计 |
| `ry /` | YMODEM 接收模式（上传脚本会自动调用） |

详细用法、限制与排障见 [MIDI播放器使用说明.md](MIDI播放器使用说明.md)。

## 编译

需要 RT-Thread v5.3.0 源码、ARM GCC 工具链和 scons。编译脚本在 `tools/build.sh`，按其中的路径配置 `RTT_EXEC_PATH` 与 scons 位置后在 BSP 目录执行 `scons` 即可，产物为 `rtthread.bin`。

## 目录结构

- `rt-thread/` — v5.3.0 内核与裁剪后的组件（仅保留本工程编译所需）
- `rt-thread/bsp/stm32/stm32l452-st-nucleo/` — BSP 与应用源码（`applications/` 下为播放器核心）
- `tools/` — 上传、串口命令、编译与清理脚本

## 许可

基于 RT-Thread（Apache-2.0）与 Playtune、miditones 开源代码实现。
