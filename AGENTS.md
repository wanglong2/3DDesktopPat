# Repository Guidelines

## Project Structure & Module Organization

`CMakeLists.txt` defines the `crose_pet` executable and pinned dependencies. `src/main.cpp` owns the pet loop; `configurator.cpp`, `pet_config.cpp`, and `model_render.cpp` handle the startup UI, JSON profiles, and per-action component rendering. The tray interface is `src/system_tray.h`, with Windows and stub backends. `config/pet.json` is the primary sample profile; legacy `pet.ini` remains supported. `tests/` contains profile persistence checks. Model files (`*.glb`, `*.gltf`, `*.bin`) are ignored by Git.

## Build, Test, and Development Commands

Use CMake 3.22+ and a C++17 compiler. The first configure downloads pinned dependencies; Linux also needs the X11 and OpenGL development packages described in the README.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
./build/crose_pet --config ./build/pet.ini
ctest --test-dir build --output-on-failure
```

On Windows, run `build/crose_pet.exe`. The sample `model = ../团子.glb` refers to a local, untracked asset. Set `model =` in `build/pet.ini` to use the built-in demo pet when that file is unavailable. Reconfigure CMake after editing `config/pet.ini` to refresh the copied file.

## Coding Style & Naming Conventions

Follow the surrounding code: four-space indentation, C++17 in `main.cpp`, and C for the tray backends. C++ uses `camelCase` for functions and variables and `PascalCase` for types; the C interface uses `crose_` prefixes with `snake_case`. Keep platform-specific behavior in the appropriate tray source file. No formatter or linter is configured; build with the existing CMake warning flags (`/W4` on MSVC, `-Wall -Wextra -Wpedantic` elsewhere).

## Testing Guidelines

Run `ctest --test-dir build --output-on-failure` for JSON profile tests. In a graphical session, run `./build/crose_pet --config ./build/pet.json --smoke-test` for the three-second animation and render check. For imported models, run `--list-animations` with the same config and verify the configured clips. Manually check the startup editor, click, drag, scroll, and exit behavior; on Windows, also check tray show/hide and transparent-area click-through.

## Commit & Pull Request Guidelines

Recent commits use a short `feat: description` subject. Follow that `type: concise summary` pattern (for example, `fix: correct tray toggle`) and keep each commit focused. In a pull request, describe the user-visible change, affected platforms, and validation commands. Link a related issue when one exists, and include a screenshot or short recording for visual or tray changes.
