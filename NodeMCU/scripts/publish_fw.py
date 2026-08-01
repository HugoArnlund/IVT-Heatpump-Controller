Import("env")

from pathlib import Path
from shutil import copy2
from datetime import datetime


def publish_firmware(source, target, env):
    project_dir = Path(env.subst("$PROJECT_DIR"))
    build_dir = Path(env.subst("$BUILD_DIR"))
    firmware_source = build_dir / "firmware.bin"

    version = env.get("FW_VERSION_VALUE")
    if not version:
        version = datetime.now().strftime("%Y%m%d%H%M")

    firmware_dir = project_dir.parent / "web" / "static" / "fw"
    firmware_dir.mkdir(parents=True, exist_ok=True)

    versioned_firmware = firmware_dir / version
    latest_version_file = firmware_dir / "latest.txt"

    copy2(firmware_source, versioned_firmware)
    latest_version_file.write_text(version + "\n", encoding="utf-8")

    # Keep only the 5 newest firmware files
    firmware_files = [
        f for f in firmware_dir.iterdir()
        if f.is_file() and f.name != "latest.txt"
    ]

    firmware_files.sort(key=lambda f: f.stat().st_mtime, reverse=True)

    for old_firmware in firmware_files[5:]:
        old_firmware.unlink()
        print(f"Removed old firmware: {old_firmware.name}")

    print(f"Published firmware to {versioned_firmware}")
    print(f"Updated {latest_version_file}")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", publish_firmware)