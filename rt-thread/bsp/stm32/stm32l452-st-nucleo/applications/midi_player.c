/*
 * MIDI player commands for RT-Thread 5.3.0 + Playtune.
 *   midiplay  - convert /music.mid on-board (miditones) and play via TIM1 PWM
 *   builtin   - play the built-in bfxk demo score
 */
#include <rtthread.h>
#include <dfs.h>
#include "playtune.h"

extern int midi_convert(const char *mid_path, unsigned char *out_buf, long out_cap);
extern void synth_test(void);
extern volatile int32_t  audio_peak_level;
extern volatile int32_t  audio_peak_scaled;
extern volatile uint32_t audio_clip_count;
extern void audio_set_volume_q16(uint32_t v);
extern uint32_t audio_get_volume_q16(void);
extern void spwm_audio_isr_disable(void);
extern void spwm_audio_isr_enable(void);

/* bytestream buffer in RAM; must stay valid while playback runs.
 * 32 KB is enough: the largest measured bytestream here is 23889 B
 * (GOD KNOWS).  Shrinking this returns 32 KB to the heap for the
 * whole-.mid malloc(), raising the practical .mid limit from ~40 KB to ~70 KB.
 * Overflow is now detected in midi_convert(), so a too-long score is
 * rejected instead of being silently truncated. */
static unsigned char g_score[32 * 1024];
static const char *MIDI_PATH = "/music.mid";

static void midiplay(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : MIDI_PATH;
    int len;
    int fd;

    fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        rt_kprintf("[midi] file %s not found (errno %d). Upload it first.\n", path, rt_get_errno());
        return;
    }
    close(fd);

    rt_kprintf("[midi] converting %s ...\n", path);
    len = midi_convert(path, g_score, sizeof(g_score));
    if (len <= 0)
    {
        rt_kprintf("[midi] conversion failed (%d)\n", len);
        return;
    }

    rt_kprintf("[midi] playing %d-byte bytestream\n", len);
    play(g_score, 6);   /* num_tgens is overridden by the 'Pt' header */
}
MSH_CMD_EXPORT(midiplay, convert and play /music.mid);

static void builtin(int argc, char **argv)
{
    synth_test();
}
MSH_CMD_EXPORT(builtin, play built-in demo score);

/* Report peak mix level and clip count since the last call, then reset.
 * Useful to check loudness/headroom: peak near 32767 = full scale,
 * clip_count should stay low for a well-behaved score. */
static void audiostat(int argc, char **argv)
{
    int32_t peak, peak_s;
    uint32_t clips;

    peak    = audio_peak_level;
    peak_s  = audio_peak_scaled;
    clips   = audio_clip_count;
    audio_peak_level  = 0;
    audio_peak_scaled = 0;
    audio_clip_count  = 0;

    /* rt_kprintf has no float support, so compute percentages as integers. */
    rt_kprintf("[audio] mix peak (pre-vol)   = %ld / 32767\n", (long)peak);
    rt_kprintf("[audio] out peak (post-vol)  = %ld / 32767 (%ld%% of pre-vol)\n",
               (long)peak_s, peak ? (long)(peak_s * 100 / peak) : 0L);
    rt_kprintf("[audio] clipped samples since last read = %lu\n",
               (unsigned long)clips);
}
MSH_CMD_EXPORT(audiostat, show audio peak level and clip count);

/* ---------- Master volume with persistence ----------
 * volume          -> show current level
 * volume <0..100> -> set level (0 = mute, 100 = full), saved to /vol.cfg
 * The value is read back at boot by volume_load(), called from fs_init().
 */
#define VOL_CFG_PATH "/vol.cfg"

static int percent_to_q16(int pct)
{
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    /* q16 = pct * 65536 / 100, computed as pct * 655 (exact for pct<=100). */
    return pct * 655;
}

static int q16_to_percent(uint32_t q16)
{
    return (int)(q16 / 655);
}

/* Write one 3-byte volume record and read it back. Returns 0 on verified
 * success.  The caller silences the audio ISR around all of this. */
static int volume_write_once(int pct)
{
    int fd, len;
    char wbuf[8], rbuf[8] = {0};

    fd = open(VOL_CFG_PATH, O_WRONLY | O_CREAT, 0);
    if (fd < 0)
        return -1;

    lseek(fd, 0, SEEK_SET);
    len = rt_snprintf(wbuf, sizeof(wbuf), "%3d", pct); /* 3 chars, space padded */
    write(fd, wbuf, len);
    close(fd);   /* littlefs commits the page on close */

    /* verify by reading back */
    fd = open(VOL_CFG_PATH, O_RDONLY, 0);
    if (fd < 0)
        return -2;
    len = read(fd, rbuf, sizeof(rbuf) - 1);
    close(fd);
    if (len != 3 || atoi(rbuf) != pct)
        return -3;
    return 0;
}

/* Save volume with read-back verification and retries.
 * This littlefs-on-FAL port occasionally fails a flash write (notably the
 * first one after boot); retrying a few times always succeeds.  O_TRUNC is
 * broken here, so we overwrite a fixed 3-byte record via lseek(0) instead. */
static void volume_save(int pct)
{
    int attempt, rc = -1;

    spwm_audio_isr_disable();  /* TIM1 ISR must not fire during flash programming */
    for (attempt = 0; attempt < 4 && rc != 0; ++attempt)
        rc = volume_write_once(pct);
    spwm_audio_isr_enable();

    if (rc == 0)
        rt_kprintf("[volume] set to %d%% (saved)\n", pct);
    else
        rt_kprintf("[volume] WARNING: failed to save %d%% after %d tries\n", pct, attempt);
}

/* Load saved volume. Called right after littlefs is mounted. */
void volume_load(void)
{
    int fd = open(VOL_CFG_PATH, O_RDONLY, 0);
    char buf[8] = {0};
    int len, pct;

    if (fd < 0)
        return; /* no config yet: keep the 100% default */

    len = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (len <= 0)
        return;
    buf[len] = '\0';
    pct = atoi(buf);
    if (pct < 0 || pct > 100)
        return;

    audio_set_volume_q16(percent_to_q16(pct));
    rt_kprintf("[volume] restored %d%% from %s\n", pct, VOL_CFG_PATH);
}

static void volume(int argc, char **argv)
{
    int pct = q16_to_percent(audio_get_volume_q16());

    if (argc == 1)
    {
        rt_kprintf("[volume] current = %d%% (0=mute, 100=full)\n", pct);
        rt_kprintf("[volume] usage: volume <0..100>   e.g. 'volume 30' for night\n");
        return;
    }

    pct = atoi(argv[1]);
    if (pct < 0 || pct > 100)
    {
        rt_kprintf("[volume] value must be 0..100\n");
        return;
    }

    audio_set_volume_q16(percent_to_q16(pct));
    volume_save(pct);
}
MSH_CMD_EXPORT(volume, set audio volume 0-100 saved across reboot);
