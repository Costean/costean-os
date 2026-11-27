#!/usr/bin/env python3
"""costean.9 guard: code that runs in tasks with a PSRAM stack must not map or erase/write flash.
system_api_get_full_json() runs in the WebSocket API task (PSRAM stack); esp_ota_get_boot_partition() there crashed
the miner (assert s_task_stack_is_sane_when_cache_frozen). Only system_api_refresh_boot_partition() may call it."""
import re, sys, pathlib
root = pathlib.Path(__file__).resolve().parents[2]
src = (root / 'main/http_server/system_api_json.c').read_text()
bad = re.compile(r'\b(esp_ota_get_boot_partition|esp_ota_get_next_update_partition|esp_ota_get_partition_description|'
                 r'esp_partition_(mmap|read|write|erase_range)|spi_flash_\w+|esp_flash_\w+|nvs_(open|get_(?!stats)\w+|set_\w+|commit))\s*\(')
allowed = re.compile(r'void system_api_refresh_boot_partition\(void\)\s*\{.*?\n\}', re.S)
nocomment = re.sub(r'//[^\n]*|/\*.*?\*/', '', src, flags=re.S)
rest = allowed.sub('', nocomment)   # nvs_get_stats only reads NVS's RAM page table
hits = [m.group(1) for m in bad.finditer(rest)]
if hits:
    print('psram-stack flash check: FAILED, system_api_json.c calls', sorted(set(hits)))
    sys.exit(1)
print('psram-stack flash check: system_api_json.c has no flash mapping/IO outside the start-up refresh: ok')
