# Wumpo

Десктопный прототип будущей игровой консоли размером с брелок: монохромный экран
64×32, шесть кнопок и детерминированный runtime, достаточно маленький, чтобы
позже переехать на микроконтроллер.

**Статус:** экспериментальный, `0.x.y`. Две игры — «Сдвиг»
([docs/game-design/the-shift.md](docs/game-design/the-shift.md)) и «Эхо»
([docs/game-design/echo.md](docs/game-design/echo.md)). Устройство запускается
сразу в последнюю игру, в которую играли; переключение между играми — удержание
`A`+`B` вместе (см. [ADR-008](docs/decisions/ADR-008-multi-game-launcher.md)).
Ни один API не стабилен.

## Сборка

Нужны компилятор C++20, CMake 3.24+ и Ninja. На Windows — MSVC из Visual Studio
2022/2026 с рабочей нагрузкой «Разработка классических приложений на C++».

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
```

SDL3 скачивается и собирается автоматически при первой конфигурации. Всё
остальное либо вендорено, либо стандартная библиотека — менеджер пакетов не нужен.

## Запуск

```powershell
.\build\debug\emulator\wumpo.exe
```

Управление в «Сдвиге» — только `LEFT`/`RIGHT`; в «Эхе» — ещё и `A` для пинга.
Держите `A`+`B` вместе, чтобы открыть список игр и переключиться.

| Клавиша | Действие |
|---|---|
| Стрелки | LEFT / RIGHT / UP / DOWN |
| `Z`, `Enter` | A |
| `X` | B |
| `A`+`B` (держать) | Список игр / переключить игру |
| `F1` | Рестарт |
| `F3` | Отладочный оверлей |
| `F4` | Скриншот |
| `1` `2` `4` `8` | Масштаб экрана |

Ключи командной строки: `--seed N`, `--scale N`, `--record FILE`,
`--replay FILE`, `--screenshot FILE`, `--headless`, `--ticks N`.

## Структура

`src/core` — runtime и детерминированные примитивы · `src/renderer` — фреймбуфер
и рисование · `src/input` — кнопки · `src/audio` — одноканальный звук ·
`src/storage` — сохранения · `src/game` — игровая логика · `src/platform` —
аппаратная абстракция с бэкендами SDL3 и headless · `emulator` — десктопная
оболочка · `tests` — юнит-, golden- и replay-тесты.

## Документация

- [Обзор архитектуры](docs/architecture/overview.md)
- [Архитектурные решения (ADR)](docs/decisions/)
- [Стратегия тестирования](docs/testing/strategy.md)
- [Настройка тулчейна](docs/development/toolchain.md)
- [Как участвовать](CONTRIBUTING.md) · [Политика безопасности](SECURITY.md) · [Changelog](CHANGELOG.md)

## Лицензия

MIT — см. [LICENSE](LICENSE). «Wumpo» — рабочее название, юридическая проверка
товарного знака не проводилась.
