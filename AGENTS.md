# Repository Guidelines

## Project Structure & Module Organization

`CMakeLists.txt` builds the `crose_pet` executable and pins dependencies via `FetchContent` (raylib, raygui, portable-file-dialogs, nlohmann/json 3.12.0); the first configure needs network access. Source lives in `src/`:

- `main.cpp` — entry point, CLI parsing, render loop, interaction, smoke test.
- `configurator.cpp/.h` — raygui startup editor.
- `pet_config.cpp/.h` — `crose::Config`, JSON/legacy-INI I/O, validation.
- `model_render.cpp/.h` — clip playback, blending, component visibility.
- `system_tray.h` — C ABI; `system_tray_win32.c` (Windows), `system_tray_stub.c` (elsewhere).

`config/pet.json` is the primary profile; `config/pet.ini` is legacy input. `tests/` holds CTest cases. Model assets (`*.glb`, `*.gltf`, `*.bin`) are git-ignored; `团子.glb` is a local, untracked sample.

## Build, Test, and Development Commands

Use CMake 3.22+ and a C++17 compiler; Linux also needs X11/OpenGL dev packages.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release   # configure, fetch deps, copy config/
cmake --build build --config Release -j          # build crose_pet and tests
./build/crose_pet                                # open the configurator
./build/crose_pet --config build/pet.json --run  # launch a profile directly
ctest --test-dir build --output-on-failure       # run tests
```

Flags: `--list-animations` prints model clip names; `--smoke-test` runs a hidden 3-second action cycle; `--help` lists options. On Windows use `build/crose_pet.exe`; MinGW builds are `-mwindows` (no console).

## Coding Style & Naming Conventions

Four-space indentation, C++17 for application code, C for tray backends. C++ uses `camelCase` functions/variables, `PascalCase` types, and the `crose` namespace; the C interface uses `crose_`-prefixed `snake_case`. Keep OS-specific logic inside the matching tray backend. No formatter or linter is configured; build cleanly under `/W4 /utf-8` (MSVC) or `-Wall -Wextra -Wpedantic`.

## Testing Guidelines

`ctest` runs `config_roundtrip`, which saves and reloads a JSON profile and asserts name, model path, and drag-action fields survive. Add cases under `tests/`, link `src/pet_config.cpp`, and register them with `add_test`. Before submitting, also run `--smoke-test` and `--list-animations` for imported models, then manually check the configurator, click/drag/scroll/exit, and Windows tray behavior.

## Commit & Pull Request Guidelines

History follows `type: concise summary` (e.g., `feat: 添加配置界面`, `fix: correct tray toggle`); keep commits focused. Pull requests should state the user-visible change, affected platforms, and validation commands, link related issues, and include a screenshot or short recording for visual or tray changes.

## Configuration Notes

Profiles hold window, display, animation, and behavior settings plus seven actions (`startup`, `idle`, `drag`, `move`, `left_click`, `right_click`, `exit`). Animations accept a name, `|`-separated fallbacks, or `#index`. Model paths resolve relative to the profile; leave `model` empty for the demo pet. Re-run CMake after editing `config/pet.json` — files are copied at configure time.
