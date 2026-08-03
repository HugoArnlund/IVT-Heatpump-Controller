import re
import pathlib
files = list(pathlib.Path('.').rglob('*.cpp')) + list(pathlib.Path('.').rglob('*.h'))
pattern = re.compile(r'ESPLogger\.(trace|debug|info|warn|error|fatal)\s*\([^,]+,\s*"([^"]*)"')
for path in files:
    text = path.read_text(encoding='utf-8', errors='ignore')
    for m in pattern.finditer(text):
        s = m.group(2)
        if len(s) > 64:
            line = text.count('\n', 0, m.start()) + 1
            print(f'{path}:{line}: {len(s)} {s}')
