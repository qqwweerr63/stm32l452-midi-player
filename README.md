# STM32L452 MIDI 蜂鸣器播放器

这是一个基于 RT-Thread v5.3.0 的 MIDI 播放器。把电脑上的 `*.mid` 文件通过串口传到板子片内 Flash，板子负责解析，用 Playtune 合成后由 TIM1 PWM 驱动蜂鸣器发声。换歌只需要传新文件，不用重新编译固件。

## 特性

- miditones 移植到了 MCU 上，`*.mid` 文件由板子直接解析成乐谱字节流
- Playtune 六声部软件合成，用 TIM1 PWM 模拟 12-bit DAC，采样率 19531 Hz
- 片内 Flash 划出 192 KB 给 littlefs 存放歌曲
- 通过 YMODEM 传文件，音量可调，设置掉电后仍保留

## 硬件

| 项目 | 说明 |
|---|---|
| 开发板 | STM32L452 Nucleo |
| 串口 | USB ST-LINK 虚拟串口，115200 |
| 音频输出 | PA8（TIM1_CH1），接无源蜂鸣器或小扬声器，另一端接 GND |
| 烧录 | 把 `rtthread.bin` 拷进 ST-LINK 虚拟 U 盘 |

## 使用方法

先烧录固件，打开串口进入 msh，然后上传歌曲：

```bash
python tools/ymodem_send.py your.mid
```

脚本会把文件固定存为 `/music.mid`。接着执行 `midiplay` 开始播放：

```text
msh /> midiplay
```

## 命令

| 命令 | 作用 |
|---|---|
| `midiplay [路径]` | 解析并播放 `*.mid` 文件，默认 `/music.mid`，循环播放 |
| `builtin` | 播放内置示例，用于测试音频输出 |
| `volume [0..100]` | 查询或设置音量，设置后保存到 `/vol.cfg` |
| `audiostat` | 查看音频峰值和削波次数 |
| `ry /` | 进入 YMODEM 接收模式，上传脚本会自动调用 |

完整的用法、限制和常见问题见 [MIDI播放器使用说明.md](MIDI播放器使用说明.md)。

## 编译

需要 RT-Thread v5.3.0 源码、ARM GCC 工具链和 scons。`tools/build.sh` 里有路径配置，按自己的环境改好 `RTT_EXEC_PATH` 和 scons 位置，在 BSP 目录执行 `scons`，生成的固件是 `rtthread.bin`。

## 目录结构

- `rt-thread/`：v5.3.0 内核和裁剪后的组件，只保留本工程用到的部分
- `rt-thread/bsp/stm32/stm32l452-st-nucleo/`：BSP 和应用代码，播放器主体在 `applications/` 下
- `tools/`：上传、串口命令、编译和清理脚本

## 参考

播放和解析两部分移植自 Len Shustek 的开源项目：

- [Playtune_samp](https://github.com/LenShustek/Playtune_samp)：单定时器中断配合波形采样表合成声音，对应 `applications/playtune/`
- [miditones](https://github.com/LenShustek/miditones)：把 `*.mid` 文件转成紧凑字节流，对应 `applications/midi_convert.c`

## 许可

代码基于 RT-Thread（Apache-2.0）、Playtune 和 miditones 实现。
