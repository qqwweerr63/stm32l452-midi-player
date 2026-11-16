#!/usr/bin/env python3
"""Fine-grained cleanup of the RT-Thread v5.3 tree for the
stm32l452-st-nucleo MIDI player.

Keeps exactly the sources that the current rtconfig.h compiles (verified
against the .o files produced by a full build), plus the framework files
(SConscript/Kconfig) that SCons needs to walk the tree.

Usage:
    python cleanup_rtt.py --dry    # print what would be deleted, delete nothing
    python cleanup_rtt.py          # actually delete
"""
import os
import sys
import shutil

ROOT = "E:/test/rtt-v5/rt-thread"
DRY = "--dry" in sys.argv

removed = []
freed = 0


def dsize(path):
    if os.path.isfile(path):
        return os.path.getsize(path)
    total = 0
    for dp, _, fs in os.walk(path):
        for f in fs:
            try:
                total += os.path.getsize(os.path.join(dp, f))
            except OSError:
                pass
    return total


def rm(path):
    global freed
    if not os.path.exists(path):
        return
    size = dsize(path)
    removed.append((path, size))
    freed += size
    if DRY:
        return
    try:
        if os.path.isdir(path) and not os.path.islink(path):
            shutil.rmtree(path)
        else:
            os.remove(path)
    except Exception as e:
        print("  FAILED:", path, e)


def clean_except(directory, keep):
    """Delete every child of `directory` not named in `keep`."""
    if not os.path.isdir(directory):
        return
    for name in sorted(os.listdir(directory)):
        if name in keep:
            continue
        # never touch git metadata
        if name == ".git":
            continue
        rm(os.path.join(directory, name))


P = lambda *a: os.path.join(ROOT, *a)
print("mode:", "DRY RUN" if DRY else "DELETE")

# --- [1] components: keep only modules used by rtconfig.h -------------------
# Used: dfs(v1), drivers(core/ipc/mtd/pin/serial/include), fal, finsh,
#       libc(compilers/common,newlib), utilities(ymodem).
# Framework files Kconfig/SConscript stay so SCons can walk the tree.
clean_except(P("components"), {
    "SConscript", "Kconfig",
    "dfs", "drivers", "fal", "finsh", "libc", "utilities",
})

# dfs: v1 only (RT_USING_DFS_V1); inside v1 keep devfs (RT_USING_DFS_DEVFS)
clean_except(P("components", "dfs"), {"SConscript", "Kconfig", "dfs_v1"})
clean_except(P("components", "dfs", "dfs_v1"),
             {"SConscript", "include", "src", "filesystems"})
clean_except(P("components", "dfs", "dfs_v1", "filesystems"),
             {"SConscript", "devfs"})

# drivers: core(device/ipc), mtd, pin, serial + headers
clean_except(P("components", "drivers"), {
    "SConscript", "Kconfig", "include",
    "core", "ipc", "mtd", "pin", "serial",
})

# fal: src + inc only
clean_except(P("components", "fal"), {"SConscript", "Kconfig", "inc", "src"})

# libc: compilers/common + compilers/newlib only
clean_except(P("components", "libc"),
             {"SConscript", "Kconfig", "compilers"})
clean_except(P("components", "libc", "compilers"),
             {"SConscript", "common", "newlib"})

# utilities: ymodem only
clean_except(P("components", "utilities"),
             {"SConscript", "Kconfig", "ymodem"})

# --- [2] libcpu: arm only; arm: common + cortex-m4 --------------------------
clean_except(P("libcpu"),
             {"SConscript", "Kconfig", "arm"})
clean_except(P("libcpu", "arm"),
             {"SConscript", "common", "cortex-m4"})

# --- [3] tools: keep the build .py scripts, drop helper subdirs -------------
for name in sorted(os.listdir(P("tools"))):
    full = P("tools", name)
    if os.path.isdir(full):
        rm(full)

# --- [4] bsp/stm32/libraries: keep HAL_Drivers + Kconfig --------------------
clean_except(P("bsp", "stm32", "libraries"), {"HAL_Drivers", "Kconfig"})

# --- [5] BSP toolchain packages: strip their bundled .git metadata ----------
pkgroot = P("bsp", "stm32", "stm32l452-st-nucleo", "packages")
if os.path.isdir(pkgroot):
    for pkg in sorted(os.listdir(pkgroot)):
        for meta in (".git", ".git.bak"):
            g = os.path.join(pkgroot, pkg, meta)
            if os.path.isdir(g):
                rm(g)

# --- [6] BSP build residue --------------------------------------------------
bsp = P("bsp", "stm32", "stm32l452-st-nucleo")
if os.path.isdir(bsp):
    for name in sorted(os.listdir(bsp)):
        if (name.startswith("build_prev") or name.startswith("build_old")
                or ".trash" in name or name == ".sconsign.dblite"):
            rm(os.path.join(bsp, name))

print("\n=== items: %d ===" % len(removed))
for p, s in removed:
    print("  %9.2f MB  %s" % (s / 1048576.0, p.replace(ROOT, "<RTT>")))
print("=== total: %.1f MB ===" % (freed / 1048576.0))
if DRY:
    print("(dry run: nothing deleted)")
