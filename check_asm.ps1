Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
Remove-Item Env:SHELL -ErrorAction SilentlyContinue
Remove-Item Env:TERM -ErrorAction SilentlyContinue

$env:PATH = 'C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin;' + $env:PATH
Set-Location 'C:\Users\Administrator\Documents\ESP-32-project\xiaozhi-esp32\build'

Write-Host "=== ChineseTtsPlayer constructor disassembly ==="
xtensa-esp32s3-elf-objdump -d -C xiaozhi.elf 2>&1 | Select-String -Context 0,40 'ChineseTtsPlayer::ChineseTtsPlayer' | Select-Object -First 50

Write-Host "`n=== esp_tts_voice_set_init proper disassembly ==="
xtensa-esp32s3-elf-objdump -d -C --start-address=0x420499b8 --stop-address=0x42049a10 xiaozhi.elf 2>&1
