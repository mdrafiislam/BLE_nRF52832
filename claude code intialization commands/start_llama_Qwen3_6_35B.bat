@echo off
title LLaMA Server - Qwen3 Coder Next

echo =====================================
echo   Starting Qwen3-Coder-Next Server
echo =====================================
echo.

cd /d C:\llama

if not exist llama-server.exe (
    echo ERROR: llama-server.exe not found!
    pause
    exit
)

if not exist "C:\llama\models\Qwen3-Coder-Next-GGUF\Qwen3-Coder-Next-UD-Q4_K_XL.gguf" (
    echo ERROR: Model file not found!
    pause
    exit
)

echo Model found. Launching server...
echo.

llama-server.exe ^
  --model "C:\llama\models\Qwen3.6-35B-GGUF\Qwen3.6-35B-A3B-UD-Q4_K_XL.gguf" ^
  --alias "qwen36-reason" ^
  --port 8001 ^
  --ctx-size 131072 ^
  --n-gpu-layers 999 ^
  --flash-attn on ^
  --kv-unified ^
  --cache-type-k q8_0 ^
  --cache-type-v q8_0 ^
  --chat-template-kwargs "{\"enable_thinking\": true}"

pause