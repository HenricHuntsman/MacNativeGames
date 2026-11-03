#!/usr/bin/env bash
# DragonHub Linux port foundation smoke test.
# No game files, keys, GPU, audio hardware or graphical session are required.
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
port="$root/linux_port"

if ! command -v make >/dev/null || ! command -v pkg-config >/dev/null; then
  echo 'ERROR: make and pkg-config are required.' >&2
  exit 2
fi
for library in sdl2 vulkan; do
  if ! pkg-config --exists "$library"; then
    echo "ERROR: missing $library development headers (pkg-config)." >&2
    exit 2
  fi
done

printf '\n== DragonHub: synthetic VM + controller tests ==\n'
SDL_VIDEODRIVER=dummy make -C "$port" test

printf '\n== DragonHub: dummy audio test ==\n'
SDL_AUDIODRIVER=dummy make -C "$port" test-audio

printf '\n== DragonHub: compile Vulkan/SDL2 window sample ==\n'
make -C "$port" vk_clear

printf '\n== DragonHub: Python syntax checks ==\n'
python3 - <<'PY' "$port/build_linux.py"
import ast, pathlib, sys
p = pathlib.Path(sys.argv[1])
ast.parse(p.read_text(encoding='utf-8'), filename=str(p))
print('PASS:', p.name, 'syntax')
PY

printf '\nPASS: foundation smoke tests.\n'
printf 'LIMIT: This is NOT proof of Vulkan rendering, audible sound, ARM64 translation, or game execution.\n'