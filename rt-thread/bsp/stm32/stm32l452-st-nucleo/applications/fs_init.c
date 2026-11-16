/*
 * File system init for STM32L452 Nucleo - RT-Thread 5.3.0
 * Mount littlefs on the "filesystem" partition of on-chip flash.
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <fal.h>
#include <dfs_fs.h>

#define FS_PARTITION_NAME "filesystem"

extern void volume_load(void);

static int fs_init(void)
{
    struct rt_device *mtd_dev = RT_NULL;

    fal_init();

    mtd_dev = fal_mtd_nor_device_create(FS_PARTITION_NAME);
    if (!mtd_dev)
    {
        rt_kprintf("[fs] ERROR: can't create mtd device on '%s'\n", FS_PARTITION_NAME);
        return -1;
    }

    if (dfs_mount(FS_PARTITION_NAME, "/", "lfs", 0, 0) == 0)
    {
        rt_kprintf("[fs] littlefs mounted on /\n");
        volume_load();
        return 0;
    }

    /* First boot on a blank partition: format once, then mount again. */
    rt_kprintf("[fs] mount failed (errno %d), formatting %s once...\n",
               rt_get_errno(), FS_PARTITION_NAME);
    if (dfs_mkfs("lfs", FS_PARTITION_NAME) == 0 &&
        dfs_mount(FS_PARTITION_NAME, "/", "lfs", 0, 0) == 0)
    {
        rt_kprintf("[fs] littlefs formatted and mounted on /\n");
        volume_load();
        return 0;
    }

    rt_kprintf("[fs] FATAL: could not prepare littlefs.\n");
    return -1;
}
INIT_APP_EXPORT(fs_init);
