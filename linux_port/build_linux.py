#!/usr/bin/env python3
"""Linux builder for MacNativeGames, step 1: `check` detects the system and says what the port can and cannot do on it.
build_app.py stays the Mac builder. Its stages 1 (code) and 2 (data and fonts) are Python and C and carry over, stage 3
swaps the Metal steps for SPIR-V, and stage 4 writes a folder instead of a signed .app."""
import argparse, os, platform, re, shutil, subprocess, sys
from pathlib import Path
from urllib.error import URLError
from urllib.request import urlopen

NEED_DISK_GB = {'base': 25, 'with update': 40}   # the numbers the Mac guide gives for a fresh build


def sh(*cmd):
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=20)
        return r.stdout.strip() if r.returncode == 0 else ''
    except (OSError, subprocess.SubprocessError):
        return ''


def pkg_config(name):
    return sh('pkg-config', '--modversion', name) if shutil.which('pkg-config') else ''


def memory_gb():
    m = re.search(r'MemTotal:\s+(\d+) kB', Path('/proc/meminfo').read_text())
    return int(m.group(1)) / 1048576 if m else 0.0


def distro():
    try:
        return re.search(r'PRETTY_NAME="?([^"\n]+)', Path('/etc/os-release').read_text()).group(1)
    except (OSError, AttributeError):
        return 'unknown'


def gpus():
    out = sh('vulkaninfo', '--summary')
    return re.findall(r'deviceName\s*=\s*(.+)', out)


def ollama_status():
    if not shutil.which('ollama'):
        return 'not installed; optional local coding assistant, not a game runtime dependency'
    try:
        with urlopen('http://127.0.0.1:11434/api/tags', timeout=2) as response:
            import json
            names = [m.get('name', '') for m in json.load(response).get('models', [])]
        return 'running; models: ' + (', '.join(names) if names else 'none pulled')
    except (OSError, URLError, ValueError):
        return 'installed but server is not running (optional; start with `ollama serve`)'


def check(work):
    rows = []   # (status, item, detail); status is ok, warn or fail

    def add(status, item, detail):
        rows.append((status, item, detail))

    if platform.system() != 'Linux':
        add('fail', 'OS', f'{platform.system()}: this script is for Linux, build_app.py is the Mac builder')
    arch = platform.machine()
    add('ok', 'OS', f'{distro()}, kernel {platform.release()}')
    if arch in ('aarch64', 'arm64'):
        add('ok', 'CPU', f'{arch}: the game\'s ARM64 code can run directly, as on Apple silicon')
    else:
        add('fail', 'CPU', f'{arch}: the game\'s ARM64 code cannot run directly; an ARM64-to-{arch} translator is needed, '
                           'and this project has none')
    ram = memory_gb()
    add('ok' if ram >= 16 else 'warn', 'Memory', f'{ram:.1f} GB' + ('' if ram >= 16 else ' (the Mac guide recommends 16 GB)'))
    free = shutil.disk_usage(work if work.exists() else work.parent).free / 1e9
    need = NEED_DISK_GB['base']
    add('ok' if free >= NEED_DISK_GB['with update'] else 'warn' if free >= need else 'fail', 'Disk',
        f'{free:.0f} GB free for {work} (needs {need} GB, {NEED_DISK_GB["with update"]} GB with an update)')

    v = sys.version_info
    add('ok' if v >= (3, 14) else 'fail', 'Python', f'{v.major}.{v.minor}.{v.micro}' +
        ('' if v >= (3, 14) else ' (TOTK build scripts use Python 3.14 standard-library zstd support)'))
    for tool, why in (('clang', 'compile the runtime and SPIRV-Cross'), ('make', 'build hactool'),
                      ('pkg-config', 'find libraries'), ('git-lfs', 'fetch the repository\'s large-file assets')):
        add('ok' if shutil.which(tool) else 'fail', tool, shutil.which(tool) or f'missing: needed to {why}')
    sdks = sh('dotnet', '--list-sdks') if shutil.which('dotnet') else ''
    has_dotnet10 = any(line.split()[0].startswith('10.') for line in sdks.splitlines() if line.split())
    add('ok' if has_dotnet10 else 'warn', '.NET SDK', sdks.splitlines()[-1] if has_dotnet10 else
        'missing .NET 10 SDK: needed to build the Linux NativeAOT shader translator; it ships prebuilt for macOS only')

    for lib, pkg in (('SDL2', 'sdl2'), ('Vulkan', 'vulkan'), ('Opus', 'opus'), ('libvpx', 'vpx'),
                     ('libvorbis', 'vorbis'), ('libogg', 'ogg'), ('zstd', 'libzstd')):
        ver = pkg_config(pkg)
        add('ok' if ver else 'fail', lib, ver or f'missing development package for {pkg} (Debian/Ubuntu: apt install libsdl2-dev libvulkan-dev libopus-dev libvpx-dev libvorbis-dev libogg-dev libzstd-dev)')
    for tool, pkg in (('glslangValidator', 'glslang-tools'), ('spirv-val', 'spirv-tools')):
        add('ok' if shutil.which(tool) else 'fail', tool, shutil.which(tool) or
            f'missing (Debian/Ubuntu package: {pkg})')

    names = gpus()
    if names and all(re.search(r'llvmpipe|lavapipe|swiftshader|software rasterizer', name, re.I) for name in names):
        add('warn', 'GPU (Vulkan)', '; '.join(names) + ' (software renderer, compile/test only; not a playable GPU)')
    elif names:
        add('ok', 'GPU (Vulkan)', '; '.join(names))
    elif Path('/dev/dri').exists():
        add('warn', 'GPU (Vulkan)', 'a /dev/dri exists but vulkaninfo found no device (install vulkan-tools and the Mesa or NVIDIA driver)')
    else:
        add('fail', 'GPU (Vulkan)', 'no /dev/dri: no graphics device (a container or a remote machine?)')
    if Path('/dev/snd').exists() or shutil.which('pipewire') or shutil.which('pulseaudio'):
        add('ok', 'Audio', 'ALSA, PipeWire or PulseAudio found')
    else:
        add('warn', 'Audio', 'no sound server or /dev/snd found')
    display = os.environ.get('WAYLAND_DISPLAY') or os.environ.get('DISPLAY')
    add('ok' if display else 'warn', 'Display', display or 'no WAYLAND_DISPLAY or DISPLAY: no desktop session in this shell')
    ollama = ollama_status()
    add('ok' if 'running;' in ollama else 'warn', 'Ollama', ollama)

    icon = {'ok': 'OK  ', 'warn': 'WARN', 'fail': 'FAIL'}
    for status, item, detail in rows:
        print(f'[{icon[status]}] {item:<13} {detail}')
    fails = [r for r in rows if r[0] == 'fail']
    print()
    if not fails:
        print('This system meets what the Linux port needs. The port itself is not finished: the Vulkan backend is still to be written.')
    else:
        print(f'{len(fails)} blocking item(s) above.')
    return 1 if fails else 0


def audit():
    repo = Path(__file__).resolve().parents[1]
    runtime = repo / 'games' / 'totk' / 'runtime'
    gfx = repo / 'games' / 'totk' / 'third_party' / 'd2r_gfx' / 'gfx' / 'nvn'
    metal = gfx / 'nvn_mtl.m'
    objc_files = sorted(runtime.glob('*.m'))
    objc_lines = sum(len(p.read_text(errors='replace').splitlines()) for p in objc_files)
    objc_gfx_files = sorted(gfx.glob('*.m'))
    objc_gfx_lines = sum(len(p.read_text(errors='replace').splitlines()) for p in objc_gfx_files)
    metal_text = metal.read_text(errors='replace') if metal.is_file() else ''
    metal_lines = len(metal_text.splitlines())
    metal_tokens = len(re.findall(r'\bMTL4?\w+', metal_text))
    mach_pattern = re.compile(r'\b(?:mach_vm_[a-z_]+|vm_region_recurse|mach_time\w*|task_threads|mach_thread_self|mach_port_[a-z_]+|vm_read_overwrite|vm_write)\b')
    mach_files = []
    mach_total = 0
    for path in sorted(runtime.glob('*.[ch]')):
        text = path.read_text(errors='replace')
        count = len(mach_pattern.findall(text))
        if count:
            mach_files.append((path.relative_to(repo), count))
            mach_total += count
    build_app = (repo / 'build_app.py').read_text(errors='replace')
    totk_make = (repo / 'games' / 'totk' / 'Makefile').read_text(errors='replace')

    print('TOTK Linux source audit (no game data read)')
    print(f'Metal renderer: {metal.relative_to(repo)} ({metal_lines:,} lines, {metal_tokens:,} Metal API tokens)')
    print(f'Objective-C runtime files: {len(objc_files)} ({objc_lines:,} lines): ' + ', '.join(p.name for p in objc_files))
    print(f'Objective-C graphics files: {len(objc_gfx_files)} ({objc_gfx_lines:,} lines): ' + ', '.join(p.name for p in objc_gfx_files))
    print(f'Mach/Apple VM and thread references: {mach_total} across {len(mach_files)} C/header files')
    for path, count in mach_files:
        print(f'  {path}: {count}')
    print(f'Mac builder markers: build_app.py has {build_app.count("xcrun ")} xcrun and {build_app.count("codesign")} codesign references; TOTK Makefile has {totk_make.count("xcrun ")} xcrun and {totk_make.count("-framework")} framework link flags')
    print('Already integrated: SDL audio callback -> TOTK mixer ring (Linux syntax-tested).')
    print('Still standalone: vm_linux helpers, SDL gamepad adapter, Vulkan clear/readback probe.')
    print('Still missing: Linux Horizon service/loader integration, Cocoa/input replacement, full NVN-to-Vulkan renderer, Linux shader translator build, and ARM64-to-x86_64 execution.')
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = p.add_subparsers(dest='command', required=True)
    c = sub.add_parser('check', help='detect this system and report what the port needs')
    c.add_argument('--work', type=Path, default=Path.home() / 'MacNativeGames-work', help='where the build would write (disk space is checked here)')
    sub.add_parser('audit', help='measure remaining TOTK Linux port blockers in source')
    args = p.parse_args()
    return audit() if args.command == 'audit' else check(args.work)


if __name__ == '__main__':
    sys.exit(main())
