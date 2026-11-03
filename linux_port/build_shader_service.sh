#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RID="${TOTK_SHADER_RID:-}"
if [[ -z "$RID" ]]; then
  case "$(uname -m)" in
    x86_64) RID=linux-x64 ;;
    aarch64|arm64) RID=linux-arm64 ;;
    *) printf 'unsupported NativeAOT host architecture: %s\n' "$(uname -m)" >&2; exit 2 ;;
  esac
fi
case "$RID" in
  linux-x64|linux-arm64) ;;
  *) printf 'TOTK_SHADER_RID must be linux-x64 or linux-arm64, got: %s\n' "$RID" >&2; exit 2 ;;
esac
command -v dotnet >/dev/null || { echo 'dotnet 10 SDK is required' >&2; exit 1; }
OUT="${TOTK_SHADER_OUT:-$ROOT/games/totk/build/shader-service-$RID}"
DOTNET_CLI_TELEMETRY_OPTOUT=1 dotnet publish "$ROOT/games/totk/tools/totk_xlate/totk_xlate.csproj" \
  -c Release -r "$RID" -p:PublishAot=true -p:InvariantGlobalization=true \
  -p:RyuSrc="$ROOT/vendor/ryujinx" -o "$OUT"
"$OUT/totk_xlate" --serve </dev/null
printf 'built Linux SPIR-V translator: %s/totk_xlate\n' "$OUT"
