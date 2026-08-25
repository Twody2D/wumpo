# Настройка тулчейна

## Что нужно

| Инструмент | Версия | Зачем |
|---|---|---|
| MSVC | Visual Studio 2022/2026, рабочая нагрузка «Разработка классических приложений на C++» | компилятор C++20 на Windows |
| CMake | 3.24+ (проверено на 4.4) | система сборки |
| Ninja | 1.11+ | генератор, быстрые инкрементальные сборки |
| LLVM | 17+ | `clang-format` и `clang-tidy` |
| Git | 2.40+ | |

## Установка на Windows

```powershell
winget install Kitware.CMake Ninja-build.Ninja LLVM.LLVM
```

Компонент C++ для Visual Studio ставится через установщик VS и **обязательно с
правами администратора** — в тихом режиме он иначе завершится с кодом 5007:

```powershell
Start-Process -Verb RunAs "C:\Program Files (x86)\Microsoft Visual Studio\Installer\setup.exe" `
  -ArgumentList 'modify','--installPath','"<путь к установке VS>"',
                'Microsoft.VisualStudio.Workload.NativeDesktop','--includeRecommended','--passive','--norestart'
```

После установки перезапусти терминал, иначе новые пути в `PATH` не подхватятся.

Проверка:

```powershell
cmake --version ; ninja --version ; clang-format --version ; clang-tidy --version
```

`cl.exe` в общем `PATH` не появляется — это нормально. Он живёт в окружении
разработчика Visual Studio, которое `scripts\dev.ps1` поднимает сам через
`vswhere`.

## Сборка

```powershell
.\scripts\dev.ps1 build      # настраивает окружение MSVC, конфигурирует и собирает
.\scripts\dev.ps1 test
.\scripts\dev.ps1 run -- --demo
```

Либо напрямую, из окна Developer PowerShell:

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
```

## Пресеты

| Пресет | Назначение |
|---|---|
| `debug` | отладочная сборка со всеми проверками |
| `release` | оптимизированная сборка |
| `asan` | отладочная сборка с AddressSanitizer |
| `ci-core` | без эмулятора и без SDL — проверка изоляции слоёв |

`CMakeUserPresets.json` игнорируется гитом: локальные пресеты можно держать там,
не трогая общие.

## Зависимости

SDL3 скачивается `FetchContent` при первой конфигурации и собирается один раз;
дальше берётся из кэша сборки. Интернет нужен только при первой конфигурации.
Версия зафиксирована тегом в `cmake/dependencies.cmake`.

doctest вендорен файлом `tests/third_party/doctest.h` и не скачивается вообще.

## Частые проблемы

**`cl.exe` не найден при конфигурации.** Ты в обычном PowerShell, а не в
Developer-окружении. Используй `scripts\dev.ps1` или запусти
`Developer PowerShell for VS`.

**CMake ругается на `cmake_minimum_required` при сборке зависимости.** Это
конфликт CMake 4.x со старым проектом. Именно поэтому используется SDL3 —
см. [ADR-006](../decisions/ADR-006-sdl3-over-sdl2.md).

**Установщик Visual Studio завершается с кодом 5007.** Тихий режим требует прав
администратора с самого начала. Перезапусти через `Start-Process -Verb RunAs`.
