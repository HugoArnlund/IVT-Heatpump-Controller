Import("env")

from datetime import datetime

version = datetime.now().strftime("%Y%m%d%H%M")

env["FW_VERSION_VALUE"] = version
env.Append(CPPDEFINES=[("FW_VERSION", int(version))])

print(f"Generated FW_VERSION={version}")