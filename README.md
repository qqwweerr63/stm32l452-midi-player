# STM32L452 MIDI 蜂鸣器播放器

基于 RT-Thread v5.3.0 的单片机 MIDI 播放器。电脑上的 `.mid` 通过串口传进板子片内 Flash，由板子自己解析，再用 Playtune 合成、TIM1 PWM 推动蜂鸣器出声。换歌只要传个文件，不用重新编译烧录。

## 能做什么

- 把 miditones 移植到了板子上，`.mid` 直接在 MCU 里转成乐谱字节流
- Playtune 六声部软件合成，TIM1 PWM 当 12-bit DAC 用，采样率 19531 Hz
- 片内 Flash 划出 192 KB 给 littlefs 存歌
- YMODEM 串口传文件，音量可以软件调，设置掉电不丢

## 硬件

| 项目 | 说明 |
|---|---|
| 开发板 | STM32L452 Nucleo |
| 串口 | USB ST-LINK 虚拟串口，115200 |
| 音频输出 | PA8（TIM1_CH1），接无源蜂鸣器或小扬声器，另一端接 GND |
| 烧录 | 把 `rtthread.bin` 拷进 ST-LINK 虚拟 U 盘 |

## 上手

1. 烧录固件，打开串口进 msh。
2. 传歌（脚本会把文件固定存成 `/music.mid`）：
   ```bash
   python tools/ymodem_send.py your.mid
   ```
3. 播放：
   ```text
   msh /> midiplay
   ```

## 命令

| 命令 | 作用 |
|---|---|
| `midiplay [路径]` | 解析并播放 `.mid`，不写路径就放 `/music.mid`，播完自动循环 |
| `builtin` | 放内置示例，用来快速确认音频链路是通的 |
| `volume [0..100]` | 不带参数是查，带参数是设，设完自动存到 `/vol.cfg` |
| `audiostat` | 看峰值和削波统计 |
| `ry /` | YMODEM 接收模式，上传脚本会自己调 |

更多用法、限制和排障见 [MIDI播放器使用说明.md](MIDI播放器使用说明.md)。

## 编译

需要 RT-Thread v5.3.0 源码、ARM GCC 工具链和 scons。`tools/build.sh` 里配好了路径，改一下 `RTT_EXEC_PATH` 和 scons 的位置，在 BSP 目录跑 `scons`，出来的是 `rtthread.bin`。

## 目录

- `rt-thread/` — v5.3.0 内核和裁剪过的组件，只留了本工程编译要用到的
- `rt-thread/bsp/stm32/stm32l452-st-nucleo/` — BSP 和应用源码，播放器核心在 `applications/` 下
- `tools/` — 上传、串口命令、编译和清理脚本

## 参考

本工程的播放和解析部分移植自 Len Shustek 的开源作品：

- 播放合成：[Playtune_samp](https://github.com/LenShustek/Playtune_samp) — 单定时器中断加波形采样表合成，本工程的 `applications/playtune/` 由它移植
- MIDI 解析：[miditones](https://github.com/LenShustek/miditones) — 把 `.mid` 转成紧凑字节流，本工程的 `applications/midi_convert.c` 由它移植

## 许可

基于 RT-Thread（Apache-2.0）以及上面的 Playtune、miditones 开源代码实现。
