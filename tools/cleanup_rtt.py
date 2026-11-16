#!/usr/bin/env python3
"""Clean the RT-Thread v5.3 source tree, keeping only what is needed to
build and run the stm32l452-st-nucleo MIDI player BSP.

Usage:
    python cleanup_rtt.py --dry    # print what would be deleted, delete nothing
    python cleanup_rtt.py          # actually delete
"""
import os
import sys
import shutil

ROOT = "E:/test/rtt-v5/rt-thread"
DRY = "--dry" in sys.argv

# Top level: keep only the kernel/framework dirs that SConstruct & Kconfig load,
# plus git metadata and the WorkBuddy project log.
ROOT_KEEP = {
    ".git", ".workbuddy", "Kconfig", "src", "libcpu", "components",
    "include", "tools", "LICENSE", ".gitignore",
    "bsp",  # MUST keep: cleaned in-depth in steps [2][3][4] below
}

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
    if not os.path.isdir(directory):
        return
    for name in sorted(os.listdir(directory)):
        if name in keep:
            continue
        rm(os.path.join(directory, name))


print("mode:", "DRY RUN" if DRY else "DELETE")

print("\n[1] rt-thread/ top level (keep core dirs + .git + .workbuddy)")
clean_except(ROOT, ROOT_KEEP)

print("[2] bsp/ (keep only stm32)")
clean_except(os.path.join(ROOT, "bsp"), {"stm32"})

print("[3] bsp/stm32/ (keep libraries + stm32l452-st-nucleo)")
clean_except(os.path.join(ROOT, "bsp", "stm32"),
             {"libraries", "stm32l452-st-nucleo"})

print("[4] BSP build residue")
bsp = os.path.join(ROOT, "bsp", "stm32", "stm32l452-st-nucleo")
if os.path.isdir(bsp):
    for name in sorted(os.listdir(bsp)):
        # keep the current build/ (fresh artifacts); drop only stale backups
        if (name.startswith("build_prev") or name.startswith("build_old")
                or ".trash" in name or name == ".sconsign.dblite"):
            rm(os.path.join(bsp, name))

print("\n=== items: %d ===" % len(removed))
for p, s in removed:
    print("  %9.2f MB  %s" % (s / 1048576.0, p.replace(ROOT, "<RTT>")))
print("=== total: %.1f MB ===" % (freed / 1048576.0))
if DRY:
    print("(dry run: nothing deleted)")
