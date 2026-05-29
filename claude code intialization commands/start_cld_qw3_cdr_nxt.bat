@echo off
title Claude Code (Local)

set ANTHROPIC_BASE_URL=http://localhost:8001
if not defined ANTHROPIC_API_KEY set ANTHROPIC_API_KEY=local-no-key-required
set ANTHROPIC_MODEL=qwen3-coder-next

claude
