# Repository Guidelines

## Project Structure & Module Organization

This repository contains ESP32-S3 firmware for the M5Stack Cardputer ADV.

For detailed architecture and agent-specific implementation guidance, see [`CLAUDE.md`](./CLAUDE.md). This contributor guide complements that document with repository-wide contribution practices.

- `main/` contains the application entry point, hardware abstraction layer (`hal/`), Mooncake apps (`apps/`), and embedded assets.
- `components/` and `managed_components/` contain fetched dependencies; avoid editing vendored code directly.
- `disabled_apps/` keeps apps that are intentionally excluded from the current build.
- `docs/` contains migration and design notes; `tools/` contains developer scripts.
- `sdkconfig.defaults`, `partitions.csv`, and the root CMake files define shared build configuration.

New apps normally use `main/apps/app_<feature>/app_<feature>.{h,cpp}`, are included through `main/apps/apps.h`, and are installed from `main/main.cpp`. The recursive source glob means no CMake change is usually needed.

## Build, Test, and Development Commands

Use ESP-IDF 5.4.2 with `IDF_PATH` configured:

```bash
python3 ./fetch_repos.py  # Fetch/update declared dependencies
idf.py build              # Configure and compile firmware
idf.py flash              # Flash the connected Cardputer
idf.py monitor            # View serial output during hardware testing
```

For controller-related work, recreate the optional local links with `powershell -ExecutionPolicy Bypass -File .\tools\link-esp32-test.ps1`. Keep that sibling PlatformIO/Arduino workspace separate from this ESP-IDF build.

## Coding Style & Naming Conventions

Format C/C++ with the repository `.clang-format`: Google-based style, 4-space indentation, spaces (not tabs), and a 120-column limit. Use descriptive `snake_case` filenames and `app_<feature>` directories. Follow the existing `AppAbility` lifecycle (`onOpen`, `onRunning`, `onClose`), render through `GetHAL().canvas`, and disconnect keyboard event handlers during `onClose()`.

## Testing Guidelines

There is no automated unit-test suite. Every change must at least pass `idf.py build`; firmware and UI/input changes should also be flashed and exercised on hardware. Record relevant device behavior, serial logs, or screenshots in the pull request.

## Commit & Pull Request Guidelines

Use concise Conventional Commit-style subjects, such as `feat: add settings app` or `fix: handle home button wake`. Keep commits focused. Pull requests should explain the behavior change, identify affected apps or HAL code, report build and hardware checks, link related issues, and include screenshots or logs for user-visible changes.

## Configuration & Dependency Notes

Do not commit machine-specific `sdkconfig` changes or generated build output. Put shared configuration defaults in `sdkconfig.defaults`. Re-run `fetch_repos.py` when dependency definitions change, and document any required external repository or protocol updates.
