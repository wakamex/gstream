"""Puts a new gstream in place of the running one: windows_update.py FOLDER

Run on Windows in the signed-in user's session, where tools/windows-update sends it with deskrun.
FOLDER, in the user's home folder unless it is a whole path, holds gstream.exe and, beside it, the
new files in a folder named new. The running program is asked to close, so it saves its settings
and the channel it is playing, and ended if it has not within 10 seconds; the new files take the old
ones' places, and the program is started again on the same data, going on with that channel when it
closed by itself.
"""

import os
import shutil
import subprocess
import sys
import time

folder = os.path.join(os.path.expanduser("~"), sys.argv[1])
quiet = {"capture_output": True, "creationflags": 0x08000000}


def running():
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq gstream.exe", "/NH"], text=True, **quiet).stdout
    return "gstream.exe" in out


subprocess.run(["taskkill", "/IM", "gstream.exe"], **quiet)
closed = False
for _ in range(40):
    if not running():
        closed = True
        break
    time.sleep(0.25)
else:
    subprocess.run(["taskkill", "/IM", "gstream.exe", "/F"], **quiet)
new = os.path.join(folder, "new")
error = None
for attempt in range(120):  # (the program's file can be replaced once the program has gone)
    try:
        for name in os.listdir(new):
            shutil.copyfile(os.path.join(new, name), os.path.join(folder, name))
        break
    except OSError as e:
        error = e
        time.sleep(0.25)
else:
    sys.exit(f"the new files could not be put in place: {error!r}")
shutil.rmtree(new)
subprocess.Popen([os.path.join(folder, "gstream.exe")] + (["--resume"] if closed else []), cwd=folder, creationflags=0x00000008 | 0x00000200)  # (on its own, not the worker's child)
