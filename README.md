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

## 实现

整条链路分存储、解析、播放三段。

存储用 FAL 把片内 Flash 划出 192 KB 作 `filesystem` 分区，通过 MTD NOR 挂 littlefs，根目录 `/` 直接读写上传的 `*.mid`。

解析部分是移植到 MCU 上的 miditones。它把整份 `*.mid` 读进堆内存，转成紧凑的乐谱字节流（音符开关、换乐器、延时），写进 32 KB 缓冲。

播放由 TIM1 更新中断驱动，80 MHz / 4096 = 19531 Hz，每次中断调一次合成器的 `update()`。它按字节流推进最多 6 个声部，每个声部用 256 点单周期波形表做相位累加，混音后限幅到 0..4095 写进 TIM1_CH1 的比较寄存器，PWM 在这里当成 12-bit DAC 用。

## 相对原作的改动

播放和解析分别移植自 Playtune_samp 和 miditones。下面是移植和调试过程中相对原版做过的调整：

- **力度**：原版本来就按 MIDI velocity 发声。之前为排查多音符声音偏小，曾把力度强制拉满，那只是临时改动。现在去掉它，恢复按 velocity 处理，并用 sqrt 曲线让中间力度听起来更自然。
- **混音**：原版混音表偏保守，6 音齐鸣时每声部被压到 25%，和弦明显偏小，这也是当初把力度拉满的原因。改用 1/√N 缩放，让总响度基本不随声部数变化。
- **音量**：加了一级软件音量。缩放作用在有符号混音信号上，不动 PWM 的 50% 静音点，避免蜂鸣器引入直流。设置存 `/vol.cfg`，开机恢复。
- **溢出检测**：miditones 的输出改写到内存缓冲，超过 32 KB 会明确报 OVERFLOW 并拒绝转换，不会静默截断导致播放跑飞。
- **Flash 写入**：保存音量前先关 TIM1 中断，写完读回校验，避免 19.5 kHz 的中断打断 Flash 编程。
- **PWM 初始化**：新 BSP 没有 CubeMX 生成的初始化代码，PWM 部分由 `spwm.c` 自己配置 TIM1 时钟、PA8 复用和中断。

## 参考

播放和解析两部分移植自 Len Shustek 的开源项目：

- [Playtune_samp](https://github.com/LenShustek/Playtune_samp)：单定时器中断配合波形采样表合成声音，对应 `applications/playtune/`
- [miditones](https://github.com/LenShustek/miditones)：把 `*.mid` 文件转成紧凑字节流，对应 `applications/midi_convert.c`

## 许可

代码基于 RT-Thread（Apache-2.0）、Playtune 和 miditones 实现。
