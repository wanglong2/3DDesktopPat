# Vendored dependencies

These files are checked into the project so CMake can configure without downloading packages.
Only the headers and build sources used by `crose_pet` are included; upstream examples,
tools, and Git metadata are omitted. Keep the license files with each dependency.

| Directory | Upstream | Version | License |
| --- | --- | --- | --- |
| `raylib/` | https://github.com/raysan5/raylib | commit `02566453880e06bec97f5b8160fa6fe1e98397ca` (6.2) | `raylib/LICENSE` |
| `raygui/` | https://github.com/raysan5/raygui | commit `25c8c65a6e5f0f4d4b564a0343861898c6f2778b` | `raygui/LICENSE` |
| `portable-file-dialogs/` | https://github.com/samhocevar/portable-file-dialogs | commit `8ccf2a93f70d021842e72fd950eac2a0bb244eec` | `portable-file-dialogs/COPYING` |
| `nlohmann-json/` | https://github.com/nlohmann/json | release 3.12.0 | `nlohmann-json/LICENSE.MIT` |

Raylib's bundled GLFW and other source dependencies remain under `raylib/src/external/`.
