#!/usr/bin/env bash
# Sets up a Debian/Ubuntu Linux PC for the MacNativeGames Linux port: packages, the repo, and a system check.
# Usage: setup_pc.sh [--with-ollama] [--dir PATH]
set -euo pipefail

DIR="$HOME/MacNativeGames"
REPO="https://github.com/HenricHuntsman/MacNativeGames.git"
WITH_OLLAMA=0
while [ $# -gt 0 ]; do
  case "$1" in
    --with-ollama) WITH_OLLAMA=1 ;;
    --dir) DIR="$2"; shift ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

if ! command -v apt-get >/dev/null; then
  echo "This script requires Debian/Ubuntu apt. On another distro install the equivalent development packages manually." >&2
  exit 1
fi
PKGS="build-essential clang make pkg-config git git-lfs curl ca-certificates python3 python3-venv libsdl2-dev libvulkan-dev vulkan-tools \
libopus-dev libvpx-dev libvorbis-dev libogg-dev glslang-tools spirv-tools libzstd-dev"
PKGS="$PKGS zlib1g-dev"
echo "Installing: $PKGS"
sudo apt-get update
sudo apt-get install -y $PKGS
git lfs install

if ! (dotnet --list-sdks 2>/dev/null | awk '$1 ~ /^10\./ { found=1 } END { exit !found }'); then
  DOTNET_INSTALL="$(mktemp)"
  curl -fsSL https://dot.net/v1/dotnet-install.sh -o "$DOTNET_INSTALL"
  bash "$DOTNET_INSTALL" --channel 10.0 --install-dir "$HOME/.dotnet"
  rm -f "$DOTNET_INSTALL"
fi
export DOTNET_ROOT="$HOME/.dotnet"
export PATH="$DOTNET_ROOT:$PATH"

if [ -d "$DIR/.git" ]; then
  git -C "$DIR" pull --ff-only
  git -C "$DIR" lfs pull
elif [ -d "$DIR" ] && [ -n "$(find "$DIR" -mindepth 1 -maxdepth 1 -print -quit)" ]; then
  echo "$DIR exists but is not a Git checkout. Choose another destination with --dir PATH; existing files were left untouched." >&2
  exit 1
else
  git clone "$REPO" "$DIR"
  git -C "$DIR" lfs pull
fi

# Ollama runs the AI model on this PC; it is optional and only installed when asked for.
if [ "$WITH_OLLAMA" = 1 ]; then
  if ! command -v ollama >/dev/null; then
    echo "Installing Ollama with its official installer (https://ollama.com/install.sh)"
    curl -fsSL https://ollama.com/install.sh | sh
  fi
  ollama pull qwen2.5-coder:7b
fi

echo
python3 "$DIR/linux_port/build_linux.py" check || true
echo
echo "Next: cd $DIR/linux_port && make && ./vk_clear   (opens a window and cycles colours on your GPU)"
