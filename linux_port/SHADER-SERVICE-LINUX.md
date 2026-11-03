# Linux TOTK shader translator: status and protocol testing

The `linux_port/build_shader_service.sh` target publishes
`games/totk/tools/totk_xlate` as a Linux NativeAOT executable using .NET 10
and the vendored Ryujinx shader translator.

```bash
cd linux_port
make shader-service-linux
# Or rerun just the protocol test after a successful publish:
python3 test_shader_service_protocol.py ../games/totk/build/shader-service-linux-x64/totk_xlate
```

The protocol smoke test is **game-free**. It sends two malformed requests and two
jobs pointing to nonexistent synthetic shader files into a single
`totk_xlate --serve` process, then requires four error replies and a clean exit
on EOF. An invalid job must not kill the long-running service.

This test does **not** establish that the translator can convert a Maxwell shader
into valid SPIR-V. That still requires an independently constructed, properly
licensed synthetic shader fixture, checking the emitted `.spv` against SPIR-V
validation tools, and preferably comparing expected reflection. A successful
NativeAOT build is not a game launch or a Vulkan renderer.

The remaining game port blockers include Linux Horizon services, code loader,
NVN-to-Vulkan, window/controller runtime integration, and ARM64-to-x86_64
translation for Ryzen hosts. The six NativeAOT trimming/reflection warnings
observed in the Codespaces log remain to be triaged independently.
