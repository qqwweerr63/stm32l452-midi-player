# STM32L452 MIDI 蜂鸣器播放器使用说明

适用于本仓库的 STM32L452 Nucleo BSP（`rt-thread/bsp/stm32/stm32l452-st-nucleo`），基于 RT-Thread v5.3.0。

这套固件把电脑上的 `*.mid` 文件通过串口传到板子片内 Flash，由板子解析成乐谱字节流，再用 Playtune 合成、TIM1 PWM 驱动蜂鸣器发声。不需要在电脑上把 `*.mid` 转成 C 数组，换歌也不用重新编译烧录。

## 1. 硬件与连接

| 项目 | 说明 |
|---|---|
| 开发板 | STM32L452 Nucleo |
| 串口 | USB ST-LINK 虚拟串口，波特率 115200（端口名以本机实际为准，Windows 下通常是 COMx） |
| 音频输出 | PA8（TIM1_CH1），接蜂鸣器或小扬声器 |
| 播放方式 | TIM1 PWM 当 12-bit DAC，采样率 19531 Hz，6 声部混音 |
| 文件存储 | 片内 Flash 的 `filesystem` 分区（192 KB），挂载为根目录 `/`，文件系统 littlefs |
| 烧录 | ST-LINK 虚拟 U 盘，把 `rtthread.bin` 拷进去即自动烧录并复位 |

蜂鸣器一端接 PA8，另一端接 GND。用无源蜂鸣器或小型扬声器效果最好。PWM 静音点是 50% 占空比，输出带直流偏置保护，正常接法不会损坏器件。

## 2. 播放一首歌

前提：固件已烧录，板子已复位，串口工具已打开板子的虚拟串口（115200）。下面用 Python 脚本操作串口，也可以手敲命令。以下命令都在仓库根目录执行。

**第 1 步：上传 `*.mid` 文件**

```bash
python tools/ymodem_send.py your.mid
```

脚本通过 YMODEM 把文件发到板子，并把板上的文件名固定为 `/music.mid`。看到 `transfer complete` 即成功。

脚本依赖 `pyserial`。`tools/ymodem_send.py` 顶部的 `PORT` 要设为本机实际的串口号。

**第 2 步：复位板子**

换歌前建议复位一次，原因见第 6 节。把 `rtthread.bin` 再往 ST-LINK 虚拟 U 盘里拷一次即可自动烧录复位。

**第 3 步：播放**

在 msh 串口里执行：

```text
midiplay
```

正常输出：

```text
[midi] converting /music.mid ...
[midi] converted: 2 tracks, 3445 bytes, 437 notes, 6 tgens, 86.788 sec
[midi] playing 3445-byte bytestream
```

然后蜂鸣器开始播放，播完自动循环。

## 3. 串口（msh）命令

打开板子的虚拟串口（115200），回车进入 msh 命令行（`msh />`）。常用命令如下：

| 命令 | 作用 | 示例 |
|---|---|---|
| `midiplay` | 转换并播放 `/music.mid`（默认路径） | `midiplay` |
| `midiplay <路径>` | 转换并播放指定文件 | `midiplay /test.mid` |
| `builtin` | 播放固件内置示例乐谱，不用文件，用于测试音频链路 | `builtin` |
| `volume` | 查询当前音量 | `volume` |
| `volume <0..100>` | 设置音量并自动保存，掉电保留 | `volume 30` |
| `audiostat` | 读取音频峰值/削波统计（读后清零），用于诊断响度 | `audiostat` |
| `ry /` | 进入 YMODEM 接收模式，从串口收文件到根目录 | 由 `ymodem_send.py` 自动调用 |
| `ls` | 列出根目录文件 | `ls` |
| `cat <文件>` | 查看文本内容，如 `cat /vol.cfg` | `cat /vol.cfg` |
| `rm <文件>` | 删除文件 | `rm /old.mid` |
| `echo 内容 文件` | 写入文本文件 | `echo 50 /vol.cfg` |
| `mkfs -t lfs filesystem` | 格式化文件系统分区，会清空所有上传的歌，慎用 | 见第 6 节 |
| `help` | 列出所有命令 | `help` |
| `version` | 查看 RT-Thread 版本 | `version` |

也可以不打开串口终端，用脚本批量发命令（在仓库根目录执行）：

```bash
python tools/msh_cmd.py "ls" "volume 30" "midiplay"
```

## 4. 音量控制

```text
volume          # 查询，例如输出 current = 60%
volume 30       # 设成 30%（晚上用）
volume 60       # 设成 60%（白天默认，当前固件已设为 60）
volume 100      # 最大
volume 0        # 静音
```

设置值保存在 `/vol.cfg`，重启后自动恢复，启动时日志会出现 `[volume] restored 60% from /vol.cfg`。音量作用在映射前的音频信号上，不改变 PWM 静音点，蜂鸣器不会有直流发热问题。晚上嫌吵就设 20 或 30，白天调回 60。

响度诊断的做法是先 `midiplay` 开始播放，几秒后执行 `audiostat`：

```text
[audio] mix peak (pre-vol)   = 30843 / 32767
[audio] out peak (post-vol)  = 8840 / 32767 (28% of pre-vol)
[audio] clipped samples since last read = 0
```

`mix peak` 接近 32767 说明力度和混音用满了动态范围。`out peak / mix peak` 的百分比应约等于当前音量设定值，可以确认音量真实生效。`clipped samples` 是削波采样数，正常曲子应为 0，音符极密集的曲子偶尔有几个也属正常，固件已有硬限幅，不会产生直流。

## 5. 编译与烧录（改代码时才需要）

平时播放音乐不需要重新编译，只有改了 C 代码才做这一步。

### 编译

在仓库根目录执行：

```bash
bash tools/build.sh
```

脚本会读取 `tools/build.sh` 里配置的 ARM GCC 工具链路径（`RTT_EXEC_PATH`）和 scons 路径，按自己的环境改好这两处即可编译。每次都是全新编译，先把旧 `build/` 改名，避免增量编译触发安全钩子。产物在 `rt-thread/bsp/stm32/stm32l452-st-nucleo/rtthread.bin`。

编译成功末尾会打印 `BUILD_EXIT=0` 和各段大小。当前占用约 ROM 214 KB / 512 KB（41%），RAM 50.8 KB / 128 KB（39%）。

### 烧录

把 `rtthread.bin` 复制到 ST-LINK 虚拟 U 盘，ST-LINK 会自动烧录并复位。打开虚拟串口可看到启动日志：

```text
- RT -     Thread Operating System
 5.3.0 build ...
[fs] littlefs mounted on /
[volume] restored 60% from /vol.cfg
hello world! RT-Thread v5.3.0 on STM32L452 Nucleo, COM7 OK.
[spwm] TIM1 PWM ready, ARR=4095, sample rate=19531 Hz
Type 'midiplay' to convert /music.mid and play, or 'builtin' for demo score.
msh />
```

日志里的 `COM7 OK` 是固件编译时写进去的问候语，和你机器上实际的串口号无关。

## 6. 限制与注意事项

### 文件大小上限

| 限制 | 数值 | 说明 |
|---|---|---|
| `*.mid` 文件大小 | 约 70 KB 以内 | 转换时要把整份 `*.mid` 读进堆内存，超出会 `no memory` |
| 生成的 bytestream | 32 KB 以内 | 超过会明确报 OVERFLOW 并拒绝转换，不会静默截断 |
| 文件系统分区 | 192 KB | 可存多首歌，注意总容量 |

实测中 `GOD KNOWS.mid`（34 KB）生成 23889 字节 bytestream，播放正常；`only my railgun.mid`（47 KB）也已验证可播。一般几 KB 到几十 KB 的曲子都没问题。

bytestream 通常比原 `*.mid` 还小（miditones 是压缩），所以 32 KB 缓冲基本不会成为瓶颈。转换选项已硬编码为等价的 `-v -i -pt -d -b`（带力度），PC 端用同样参数转换可得到完全一致的结果。

### 换歌务必先复位

播放是无限循环的（字节流末尾带 restart 命令，不会自己停）。如果在上一首还在播放时直接执行新的 `midiplay` 转换，音频中断抢占可能导致 idle 线程栈溢出、系统死机。

正确流程是：上传新文件 → 复位（把 bin 再拷一次到 U 盘） → `midiplay`。复位不会清除 Flash 里的文件，上传过的文件一直保留。

### 文件系统写入

分区首次使用会自动格式化，日志为 `[fs] littlefs formatted and mounted on /`。不要用 `fal write` 之类的底层命令乱写 `filesystem` 分区，会破坏 littlefs 元数据导致挂载失败。万一把分区写坏了，用 `mkfs -t lfs filesystem` 重新格式化，注意这会清空所有上传的歌。

设置音量时偶尔会看到一条 `[E/FAL] Partition write error`，这是首次写 flash 的固有噪音，固件内部会自动关中断并重试校验，功能不受影响，以最终读回的内容为准。

### 蜂鸣器突然不响

按这个顺序排查：

1. 执行 `builtin` 或 `midiplay`，再 `audiostat`。若峰值接近满量程，说明软件侧 PWM 正常输出，通道没坏。
2. 换一个已知好的扬声器接 PA8，能响则问题在蜂鸣器支路。
3. 检查杜邦线是否松动或虚接（最常见原因），以及蜂鸣器两端是否有电压。

## 7. 目录与文件说明

```text
<仓库根目录>/
├── MIDI播放器使用说明.md          # 本文档
├── README.md                     # 项目简介
├── tools/
│   ├── build.sh                  # 编译脚本（工具链路径在脚本里配置）
│   ├── ymodem_send.py            # 上传 *.mid 到板子（强制存为 /music.mid）
│   ├── msh_cmd.py                # 通过串口批量发 msh 命令
│   ├── test_large_midi.py        # 大文件测试（上传+播放+抓输出）
│   └── gen_overflow_midi.py      # 生成超大 bytestream 测试曲，验证溢出检测
└── rt-thread/bsp/stm32/stm32l452-st-nucleo/
    ├── rtconfig.h                # 功能开关（DFS/littlefs/FAL/YMODEM/PWM 等）
    ├── applications/
    │   ├── main.c                # 主函数，初始化 PWM
    │   ├── midi_player.c         # msh 命令：midiplay/builtin/audiostat/volume
    │   ├── midi_convert.c        # 板载 miditones：*.mid → bytestream
    │   ├── fs_init.c             # FAL + littlefs 挂载、开机恢复音量
    │   ├── spwm.c                # TIM1 PWM 输出与中断
    │   └── playtune/             # Playtune 合成器、波形表、示例乐谱
    └── board/ports/fal_cfg.h     # Flash 分区表（app 320KB + filesystem 192KB）
```

## 8. 典型使用场景

日常听歌：上传一次 `*.mid` → 复位 → `midiplay`。之后上电复位直接敲 `midiplay` 就能播，文件还在 Flash 里。

晚上安静：`volume 25` 会自动保存，白天再 `volume 60` 调回。

验证音频链路：敲 `builtin`，有声音说明 PWM、蜂鸣器、合成器都正常，问题只可能在 `*.mid` 文件或转换环节。

换一首歌：`ymodem_send.py` 传新文件 → 复位 → `midiplay`。

排查声音小或破音：播放中敲 `audiostat`，看峰值是否接近满量程、削波是否过多。
