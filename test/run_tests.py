#!/usr/bin/env python3
"""
WIREFIGHT 32X - point-to-point test runner.

Builds (if needed):
  * the game ROM (release + test build with the debug status strip)
  * the headless libretro test harness
  * the PicoDrive libretro core (emulator used for the tests)

Then runs:
  1) full point-to-point suite on the test ROM (deterministic, uses the
     debug status strip + color analysis) - proves the game boots, is
     never black, plays, wins, loses nothing and returns to title.
  2) color-only smoke test on the release ROM that ships to the user.

Writes TAP output to test/results/*.tap, frame dumps to test/results/.
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
ROOTDIR = os.environ.get("ROOTDIR", "/home/user/toolchain/opt/toolchains/sega")
RESULTS = HERE / "results"
CORE = HERE / "picodrive_libretro.so"

RED, GREEN, YELLOW, RST = "\033[91m", "\033[92m", "\033[93m", "\033[0m"


def sh(cmd, cwd=None, env=None, check=True):
    print(f"{YELLOW}$ {' '.join(str(c) for c in cmd)}{RST}")
    e = os.environ.copy()
    if env:
        e.update(env)
    p = subprocess.run([str(c) for c in cmd], cwd=cwd, env=env or e,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       text=True)
    if p.stdout:
        print(p.stdout[-4000:])
    if check and p.returncode != 0:
        print(f"{RED}FAILED (rc={p.returncode}): {cmd}{RST}")
        sys.exit(p.returncode)
    return p


def ensure_built():
    if not (RESULTS).exists():
        RESULTS.mkdir(parents=True)
    sh(["make", "-C", ROOT, "generated"])
    sh(["make", "-C", ROOT, f"ROOTDIR={ROOTDIR}"])
    sh(["make", "-C", ROOT, f"ROOTDIR={ROOTDIR}", "testbuild"])
    sh(["make", "-C", HERE])
    if not CORE.exists():
        sh(["bash", str(HERE / "build_picodrive.sh")])


def convert_dumps():
    try:
        from PIL import Image
    except ImportError:
        print("PIL not available, skipping PPM->PNG conversion")
        return
    for ppm in RESULTS.glob("*.ppm"):
        png = ppm.with_suffix(".png")
        Image.open(ppm).save(png)


def main():
    ensure_built()

    rom_test = ROOT / "build" / "wirefight-test.32x"
    rom_rel = ROOT / "build" / "wirefight.32x"
    assert rom_test.exists() and rom_rel.exists(), "ROMs were not built"

    failures = []

    print(f"\n{YELLOW}=== point-to-point suite (test rom: {rom_test.name}) ==={RST}")
    p = sh([HERE / "harness", CORE, rom_test, "--suite", "--dump", RESULTS],
           check=False)
    (RESULTS / "suite.tap").write_text(p.stdout or "")
    ok1 = p.returncode == 0
    failures += [] if ok1 else ["suite"]

    print(f"\n{YELLOW}=== release rom smoke test ({rom_rel.name}) ==={RST}")
    p = sh([HERE / "harness", CORE, rom_rel, "--smoke", "--dump", RESULTS],
           check=False)
    (RESULTS / "smoke.tap").write_text(p.stdout or "")
    ok2 = p.returncode == 0
    failures += [] if ok2 else ["smoke"]

    convert_dumps()

    print()
    if failures:
        print(f"{RED}P2P TESTS FAILED: {failures}{RST}")
        return 1
    print(f"{GREEN}ALL P2P TESTS PASSED{RST}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
