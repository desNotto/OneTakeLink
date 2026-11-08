# OneTakeLink

[Русский](#русский) · [English](#english)

---

## Русский

OneTakeLink — приложение для Windows 10/11 x64: приём видео с камеры iPhone
через приложение **LensLink Camera**, мониторинг, запись в MP4, медиатека
записей со встроенным плеером. Версия: **0.2**.

### Функции

**Подключение**
- iPhone по USB (служба Apple Mobile Device Service) или по Wi-Fi (поиск Bonjour, ручной ввод IP).
- Автоматическое переподключение при обрыве связи; статус и подсказки по устранению проблем.
- Удалённый запуск камеры на iPhone; запрос режима 1080p 60 fps H.264 при подключении.

**Мониторинг**
- Вывод видео через OpenGL, аппаратное декодирование D3D11VA / DXVA2, программный резерв.
- Фактические параметры потока: разрешение, FPS, кодек, битрейт, пропуски кадров, задержка, декодер.
- Наложения: сетка третей, центральные линии, безопасная зона 90 %, рамка кадрирования 16:9 / 9:16 / 4:5 / 1:1. В файл не записываются.
- Полноэкранный режим монитора.

**Управление камерой iPhone** (только параметры, которые сообщает устройство)
- Режим (разрешение × FPS), объектив, зум, экспокоррекция.
- Фиксация фокуса, экспозиции, баланса белого.

**Запись**
- MP4 без перекодирования: исходный поток H.264/HEVC с iPhone, без аудиодорожки.
- Начало дубля с ближайшего ключевого кадра до нажатия REC (0–4 с).
- Защита от сбоев: при аварийном завершении или отключении питания дубль восстанавливается при следующем запуске; при потере связи сохраняется записанный фрагмент.
- Проверка свободного места до и во время записи.
- Структура папок `Проект/Session_ГГГГ-ММ-ДД/ГГГГ-ММ-ДД_Проект_Take_001.mp4`.
- Таймер старта 3 / 5 / 10 с.

**Медиатека и плеер**
- Сетка миниатюр или таблица: длительность, дата, размер, разрешение, FPS, кодек, проект/сессия.
- Поиск, фильтр по проекту, фильтр «Избранное», сортировка.
- Встроенный плеер: воспроизведение, перемотка, точный переход к кадру, покадровый шаг.
- Избранное, переименование, удаление в Корзину, открытие в Проводнике и во внешнем плеере.

**Интерфейс**
- Один экран: плеер, монитор камеры, панель настроек, медиатека. Размеры областей сохраняются.

### Требования

- Windows 10 или 11, x64.
- iPhone с установленным приложением LensLink Camera (App Store).
- Для USB: iTunes или Apple Devices.
- Для Wi-Fi: iPhone и ПК в одной сети; разрешение OneTakeLink в брандмауэре Windows.

### Установка и запуск

1. Скачать архив `OneTakeLink-windows-x64`: GitHub → Actions → последний успешный запуск «Build» → Artifacts.
2. Распаковать, запустить `OneTakeLink.exe`. Установка не требуется.
3. Открыть LensLink Camera на iPhone, выбрать устройство в панели «Источник», нажать «Подключить».

### Горячие клавиши

| Клавиша | Действие |
|---|---|
| Space | REC / STOP |
| F11, двойной клик по монитору | Полноэкранный монитор |
| Esc | Выход из полноэкранного режима |
| K | Плеер: воспроизведение / пауза |
| `,` / `.` | Плеер: кадр назад / вперёд |
| Enter, двойной клик по дублю | Воспроизвести дубль |
| F2 | Переименовать дубль |
| F | Избранное |
| Delete | Удалить в Корзину (с подтверждением) |
| F5 | Обновить медиатеку |

### Данные на диске

| Что | Где | Очистка |
|---|---|---|
| Записи | папка из настроек, по умолчанию `Видео\OneTakeLink` | только пользователем |
| Настройки | `%LOCALAPPDATA%\OneTakeLink\OneTakeLink\settings.ini` | — |
| Временный файл записи `*.recording.mp4` | рядом с дублем | удаляется после остановки записи; после сбоя восстанавливается при запуске |
| Кэш миниатюр | `%LOCALAPPDATA%\OneTakeLink\OneTakeLink\cache\thumbs` | удаляется вместе с дублем; при запуске — записи старше 60 дней и сверх 256 МБ |
| Журнал | `%LOCALAPPDATA%\OneTakeLink\OneTakeLink\logs` | хранятся 2 последних сеанса, не более 10 МБ на сеанс |

Сетевые соединения — только с iPhone. Телеметрия не собирается.

### Диагностика: `otl-cli`

```bat
otl-cli devices                      :: список устройств
otl-cli monitor usb 600              :: статистика потока раз в секунду, 600 с
otl-cli record usb test.mp4 60       :: тестовая запись 60 с
otl-cli recover "D:\Video"           :: восстановить прерванные записи
```

### Статус разработки

| Этап | Состояние |
|---|---|
| 1. Модуль LensLink без OBS, проверка связи | готово |
| 2. Просмотр, запись MP4 | готово |
| 3. Медиатека, плеер, проекты и сессии | готово |
| 4. Кадрирование и экспорт (H.264/H.265, NVENC / Quick Sync / AMF) | в планах |
| 5. Безопасные зоны Instagram, ручные ISO/выдержка, окно настроек | в планах |
| 6. Проверка совместимости, установщик | в планах |

### Сборка из исходников

Требуется CMake ≥ 3.21, Qt ≥ 6.4 (Widgets, OpenGLWidgets), FFmpeg (avcodec, avformat, avutil, swscale), Visual Studio 2022.

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64 ^
      -DFFMPEG_ROOT=C:\ffmpeg-7.1.1-full_build-shared
cmake --build build
```

Эмулятор LensLink Camera для проверки без iPhone: `python tools/fake_phone.py`, затем подключение к `127.0.0.1` (`OneTakeLink --connect=wifi:127.0.0.1`).

### Лицензия

GNU GPL v3 или новее (`LICENSE`). Модуль подключения основан на коде
[LensLink](https://github.com/MyNamesEMurray/LensLink) (GPL-2.0-or-later).
Сторонние компоненты: `THIRD_PARTY_NOTICES.md`. Автор: desNotto.

---

## English

OneTakeLink is a Windows 10/11 x64 application that receives video from an
iPhone camera via the **LensLink Camera** app, monitors it, records it to MP4
and manages the recordings in a media library with a built-in player.
Version: **0.2**.

### Features

**Connection**
- iPhone over USB (Apple Mobile Device Service) or Wi-Fi (Bonjour discovery, manual IP).
- Automatic reconnection; connection status with troubleshooting hints.
- Remote camera start on the iPhone; requests 1080p 60 fps H.264 on connect.

**Monitoring**
- OpenGL video output, D3D11VA / DXVA2 hardware decoding with software fallback.
- Actual stream parameters: resolution, FPS, codec, bitrate, dropped frames, latency, decoder.
- Overlays: rule of thirds, centre lines, 90 % safe area, 16:9 / 9:16 / 4:5 / 1:1 crop frame. Never written to files.
- Full-screen monitor.

**iPhone camera control** (only parameters reported by the device)
- Format (resolution × FPS), lens, zoom, exposure compensation.
- Focus, exposure and white balance lock.

**Recording**
- MP4 without re-encoding: the iPhone's own H.264/HEVC stream, no audio track.
- A take starts at the last keyframe before REC (0–4 s earlier).
- Crash safety: takes interrupted by a crash or power loss are recovered on next start; on connection loss the recorded part is kept.
- Free-space checks before and during recording.
- Folder layout `Project/Session_YYYY-MM-DD/YYYY-MM-DD_Project_Take_001.mp4`.
- Start countdown 3 / 5 / 10 s.

**Media library and player**
- Thumbnail grid or table: duration, date, size, resolution, FPS, codec, project/session.
- Search, project filter, favourites filter, sorting.
- Built-in player: playback, seeking, frame-accurate positioning, frame stepping.
- Favourites, rename, delete to Recycle Bin, show in Explorer, open in external player.

**Interface**
- Single screen: player, camera monitor, settings panel, media library. Pane sizes are remembered.

### Requirements

- Windows 10 or 11, x64.
- iPhone with LensLink Camera (App Store).
- USB: iTunes or Apple Devices.
- Wi-Fi: iPhone and PC on the same network; OneTakeLink allowed in Windows Firewall.

### Installation and start

1. Download `OneTakeLink-windows-x64`: GitHub → Actions → latest successful "Build" run → Artifacts.
2. Unpack and run `OneTakeLink.exe`. No installation needed.
3. Open LensLink Camera on the iPhone, pick the device in the "Источник" (Source) panel, click "Подключить" (Connect).

### Keyboard shortcuts

| Key | Action |
|---|---|
| Space | REC / STOP |
| F11, double-click on monitor | Full-screen monitor |
| Esc | Leave full screen |
| K | Player: play / pause |
| `,` / `.` | Player: previous / next frame |
| Enter, double-click on a take | Play take |
| F2 | Rename take |
| F | Favourite |
| Delete | Move to Recycle Bin (with confirmation) |
| F5 | Refresh media library |

### Data on disk

| What | Where | Cleanup |
|---|---|---|
| Recordings | folder from settings, default `Videos\OneTakeLink` | by the user only |
| Settings | `%LOCALAPPDATA%\OneTakeLink\OneTakeLink\settings.ini` | — |
| Temporary recording file `*.recording.mp4` | next to the take | removed when recording stops; recovered on start after a crash |
| Thumbnail cache | `%LOCALAPPDATA%\OneTakeLink\OneTakeLink\cache\thumbs` | removed with the take; on start, entries older than 60 days and above 256 MB |
| Log | `%LOCALAPPDATA%\OneTakeLink\OneTakeLink\logs` | last 2 sessions, max 10 MB per session |

Network connections are made only to the iPhone. No telemetry.

### Diagnostics: `otl-cli`

```bat
otl-cli devices                      :: list devices
otl-cli monitor usb 600              :: stream statistics every second for 600 s
otl-cli record usb test.mp4 60       :: 60 s test recording
otl-cli recover "D:\Video"           :: recover interrupted recordings
```

### Development status

| Stage | Status |
|---|---|
| 1. LensLink module without OBS, link checks | done |
| 2. Preview, MP4 recording | done |
| 3. Media library, player, projects and sessions | done |
| 4. Cropping and export (H.264/H.265, NVENC / Quick Sync / AMF) | planned |
| 5. Instagram safe zones, manual ISO/shutter, settings window | planned |
| 6. Compatibility testing, installer | planned |

### Building from source

Requires CMake ≥ 3.21, Qt ≥ 6.4 (Widgets, OpenGLWidgets), FFmpeg (avcodec, avformat, avutil, swscale), Visual Studio 2022.

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64 ^
      -DFFMPEG_ROOT=C:\ffmpeg-7.1.1-full_build-shared
cmake --build build
```

LensLink Camera emulator for testing without an iPhone: `python tools/fake_phone.py`, then connect to `127.0.0.1` (`OneTakeLink --connect=wifi:127.0.0.1`).

### License

GNU GPL v3 or later (`LICENSE`). The connection module is based on
[LensLink](https://github.com/MyNamesEMurray/LensLink) (GPL-2.0-or-later).
Third-party components: `THIRD_PARTY_NOTICES.md`. Author: desNotto.
