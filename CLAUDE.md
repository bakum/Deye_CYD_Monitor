@AGENTS.md

## Только для Claude Code

- Windows 10: для `pio.exe` предпочтительнее инструмент Bash (Git Bash) с путём `~/.platformio/penv/Scripts/pio.exe`, для этой формы в `.claude/settings.json` есть разрешения.
- Serial-монитор интерактивный. Запускать его в фоне (`run_in_background`) с записью в файл и читать файл, подробности в skill `pio-build-flash`. В Cursor пользователь присылал логи как `@terminals/N.txt`, здесь он вставит текст или попросит снять лог.
- Общие правила проекта меняются в `AGENTS.md`, а не здесь.
