Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
Remove-Item Env:SHELL -ErrorAction SilentlyContinue
Remove-Item Env:TERM -ErrorAction SilentlyContinue

$env:PATH = 'C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin;' + $env:PATH
Set-Location 'C:\Users\Administrator\Documents\ESP-32-project\xiaozhi-esp32\build'

Write-Host "=== esp_tts_voice_xiaoxin symbol ==="
xtensa-esp32s3-elf-nm -S -t x xiaozhi.elf 2>&1 | Select-String 'esp_tts_voice_xiaoxin'

Write-Host "`n=== xiaoxin related symbols with sizes ==="
xtensa-esp32s3-elf-nm -S -t x --size-sort -r xiaozhi.elf 2>&1 | Select-String 'xiaoxin|voice_set'

Write-Host "`n=== xiaole related symbols ==="
xtensa-esp32s3-elf-nm -S -t x xiaozhi.elf 2>&1 | Select-String 'xiaole'

Write-Host "`n=== esp_tts_voice_set_init disassembly ==="
xtensa-esp32s3-elf-objdump -d xiaozhi.elf 2>&1 | Select-String -Context 0,25 'esp_tts_voice_set_init>:' | Select-Object -First 35

Write-Host "`n=== Memory map check ==="
xtensa-esp32s3-elf-objdump -h xiaozhi.elf 2>&1 | Select-String 'rodata|\.data'
