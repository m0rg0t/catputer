"""PlatformIO post-build check for the explicitly supported compact profile."""
from pathlib import Path

Import("env")

def check_size(source, target, env):
    size = Path(str(target[0])).stat().st_size
    maximum = 0x140000
    print(f"Lofi compact application: {size:,}/{maximum:,} bytes; headroom {maximum-size:,}")
    if size > maximum:
        raise RuntimeError("Application exceeds compact compatibility profile; no image published")

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", check_size)
