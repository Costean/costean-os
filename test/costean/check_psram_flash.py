#!/usr/bin/env python3
"""Guard: code that runs in tasks with a PSRAM stack must not map or erase/write flash.

The costean.1 crash: system_api_get_full_json() ran in the WebSocket API task (PSRAM stack)
and called esp_ota_get_boot_partition() -> assert s_task_stack_is_sane_when_cache_frozen.

The auto-tune searcher must follow the same rule: never esp_partition_mmap, esp_ota_get_*,
or NVS-heavy ops that map flash from a PSRAM-stacked task. The searcher itself is pure
logic (tuning.cpp). Persistence stays in the Costean task, which is created with
xTaskCreate (internal-RAM stack).
"""
import re, sys, pathlib
root = pathlib.Path(__file__).resolve().parents[2]
failed = 0

FLASH = re.compile(
    r'\b(esp_ota_get_boot_partition|esp_ota_get_next_update_partition|esp_ota_get_partition_description|'
    r'esp_partition_(mmap|read|write|erase_range)|spi_flash_\w+|esp_flash_\w+|'
    r'nvs_(open|get_(?!stats)\w+|set_\w+|commit))\s*\('
)
PSRAM_STACK = re.compile(
    r'xTaskCreate(PinnedToCore)?WithCaps\s*\(|MALLOC_CAP_SPIRAM|SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY'
)

def nocomment(src: str) -> str:
    return re.sub(r'//[^\n]*|/\*.*?\*/', '', src, flags=re.S)

# 1. Existing: system_api_json.c may only touch flash inside system_api_refresh_boot_partition().
src = (root / 'main/http_server/system_api_json.c').read_text()
allowed = re.compile(r'void system_api_refresh_boot_partition\(void\)\s*\{.*?\n\}', re.S)
rest = allowed.sub('', nocomment(src))
hits = [m.group(1) for m in FLASH.finditer(rest)]
if hits:
    print('psram-stack flash check: FAILED, system_api_json.c calls', sorted(set(hits)))
    failed = 1
else:
    print('psram-stack flash check: system_api_json.c has no flash mapping/IO outside the start-up refresh: ok')

# 2. The searcher (tuning.cpp / tuning.h) must be pure: no flash, NVS, or OTA APIs at all.
for rel in ('main/costean/tuning.cpp', 'main/costean/tuning.h'):
    text = nocomment((root / rel).read_text())
    hits = [m.group(1) for m in FLASH.finditer(text)]
    if hits:
        print(f'psram-stack flash check: FAILED, {rel} (searcher) calls', sorted(set(hits)))
        failed = 1
    else:
        print(f'psram-stack flash check: {rel} has no flash/NVS/OTA calls: ok')

# 3. Costean task must stay on an internal-RAM stack (xTaskCreate, not a SPIRAM-capable create).
core = (root / 'main/costean/costean_core.cpp').read_text()
if not re.search(r'xTaskCreate\s*\(\s*task\s*,\s*"costean"', core):
    print('psram-stack flash check: FAILED, costean task is not created with xTaskCreate("costean")')
    failed = 1
else:
    print('psram-stack flash check: costean task still uses xTaskCreate (internal-RAM stack): ok')
# NVS save/load in costean_core.cpp is allowed because it runs on that task only.
if 'NVS (only from the Costean task' not in core and 'internal-RAM stack' not in core:
    print('psram-stack flash check: FAILED, costean_core.cpp lost the internal-RAM NVS comment/guard')
    failed = 1

# 4. No PSRAM-stacked tasks introduced under main/costean (including the searcher wiring).
for path in sorted((root / 'main/costean').glob('*')):
    if path.suffix not in {'.c', '.cpp', '.h'}:
        continue
    text = nocomment(path.read_text())
    hits = PSRAM_STACK.findall(text)
    # findall with groups returns tuples; also search the raw pattern
    if PSRAM_STACK.search(text):
        print(f'psram-stack flash check: FAILED, {path.relative_to(root)} mentions a PSRAM stack')
        failed = 1

# 5. A new tuner task, if one is added later, must not be the place that talks to flash.
#    Confirm there is no additional xTaskCreate in costean sources besides the known ones.
creates = []
for path in sorted((root / 'main/costean').glob('*')):
    if path.suffix not in {'.c', '.cpp'}:
        continue
    for m in re.finditer(r'xTaskCreate\w*\s*\([^;]{0,200}', nocomment(path.read_text()), re.S):
        creates.append((str(path.relative_to(root)), re.sub(r'\s+', ' ', m.group(0))[:160]))
allowed_tasks = ('"costean"', '"boot guard"')
for rel, snippet in creates:
    if not any(a in snippet for a in allowed_tasks):
        print(f'psram-stack flash check: FAILED, unexpected task in {rel}: {snippet}')
        failed = 1
if creates:
    print('psram-stack flash check: costean tasks:', ', '.join(s.split(",")[1].strip() if "," in s else s for _, s in creates), '- ok')

sys.exit(failed)
