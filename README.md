# OneTakeLink

[Русский](#русский) · [English](#english)

---

## Русский

OneTakeLink — приложение для Windows 10/11 x64: приём видео с камеры iPhone
(через приложение **LensLink Camera**) или Android-телефона (через USB-отладку,
без установки приложений), мониторинг, запись в MP4, медиатека записей со
встроенным плеером. Интерфейс на русском и английском. Версия: **0.3**.

### Функции

**Подключение iPhone**
- По USB (служба Apple Mobile Device Service) или по Wi-Fi (поиск Bonjour, ручной ввод IP).
- Удалённый запуск камеры на iPhone; запрос режима 1080p 60 fps H.264 при подключении.

**Подключение Android** (Android 12+, USB)
- Через adb и сервер scrcpy, входящие в комплект программы; на телефон ничего не устанавливается.
- Выбор камеры (основная / фронтальная / внешняя), режима (720p–4K, 30/60 fps), битрейта, кодека H.264/H.265.
- Если телефон не поддерживает 60 fps в выбранном режиме — автоматический переход на 30 fps.

**Общее**
- Автоматическое переподключение при обрыве связи; статус и подсказки по устранению проблем.

**Мониторинг**
- Вывод видео через OpenGL, аппаратное декодирование D3D11VA / DXVA2, программный резерв.
- Фактические параметры потока: разрешение, FPS, кодек, битрейт, пропуски кадров, задержка, декодер.
- Наложения: сетка третей, центральные линии, безопасная зона 90 %, рамка кадрирования 16:9 / 9:16 / 4:5 / 1:1. В файл не записываются.
- Гайдлайны соцсетей для 9:16: безопасные зоны Instagram Reels и YouTube Shorts (красные/оранжевые — интерфейс приложения перекрывает видео, зелёная — место для лиц, текста, логотипов). Зоны приблизительные, по рекомендациям Meta и Google для 1080×1920.
- Поворот кадра на 90° (кнопки в углу монитора, R / Shift+R) для телефона, закреплённого боком или вверх ногами. Поворот записывается в MP4 как метка ориентации — без перекодирования; плееры, редакторы и соцсети показывают видео повёрнутым.
- Полноэкранный режим монитора.

**Управление камерой iPhone** (только параметры, которые сообщает LensLink Camera)
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
- Верхняя панель: формат кадра, гайдлайны соцсетей, язык (RU / EN; по умолчанию — язык системы).

### Требования

- Windows 10 или 11, x64.
- iPhone: приложение LensLink Camera (App Store). Для USB — iTunes или Apple Devices; для Wi-Fi — iPhone и ПК в одной сети, OneTakeLink разрешён в брандмауэре Windows.
- Android: версия 12 или новее, включённая «Отладка по USB» (Настройки → О телефоне → 7 раз «Номер сборки» → Для разработчиков → Отладка по USB), подтверждение запроса отладки на телефоне. Для некоторых моделей нужен USB-драйвер производителя.

### Установка и запуск

1. Скачать архив `OneTakeLink-windows-x64`: GitHub → Actions → последний успешный запуск «Build» → Artifacts.
2. Распаковать, запустить `OneTakeLink.exe`. Установка не требуется.
3. iPhone: открыть LensLink Camera. Android: подключить кабелем с включённой отладкой по USB.
4. Выбрать устройство в панели «Источник», нажать «Подключить».

### Горячие клавиши

| Клавиша | Действие |
|---|---|
| Space | REC / STOP |
| F11, двойной клик по монитору | Полноэкранный монитор |
| Esc | Выход из полноэкранного режима |
| R / Shift+R | Повернуть кадр на 90° / сбросить поворот |
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
| Сервер scrcpy на Android | `/data/local/tmp/scrcpy-server.jar` | удаляется с телефона после отключения |
| Фоновый сервер adb на ПК | процесс `adb.exe` | останавливается при выходе, если его запустил OneTakeLink |

Сетевые соединения — только с iPhone. Телеметрия не собирается.

### Диагностика: `otl-cli`

```bat
otl-cli devices                      :: список устройств (iPhone и Android)
otl-cli monitor usb 600              :: статистика потока раз в секунду, 600 с
otl-cli monitor android 600          :: то же для Android
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
| — Android (scrcpy), безопасные зоны Reels / Shorts, интерфейс RU/EN | готово |
| 5. Ручные ISO/выдержка, окно настроек | в планах |
| 6. Проверка совместимости, установщик | в планах |

### Сборка из исходников

Требуется CMake ≥ 3.21, Qt ≥ 6.4 (Widgets, OpenGLWidgets), FFmpeg (avcodec, avformat, avutil, swscale), Visual Studio 2022.

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64 ^
      -DFFMPEG_ROOT=C:\ffmpeg-7.1.1-full_build-shared
cmake --build build
```

Эмуляторы для проверки без телефонов: LensLink Camera — `python tools/fake_phone.py`, затем `OneTakeLink --connect=wifi:127.0.0.1`; Android — `OTL_ADB=tools/fake_adb.py OTL_SCRCPY_SERVER=<любой файл> OneTakeLink --connect=android:`.
Переводы: `tools/update_translations.sh` (исходные строки на русском, перевод — `translations/onetakelink_en.ts`).

### Лицензия

GNU GPL v3 или новее (`LICENSE`). Модуль подключения iPhone основан на коде
[LensLink](https://github.com/MyNamesEMurray/LensLink) (GPL-2.0-or-later);
для Android используются [scrcpy](https://github.com/Genymobile/scrcpy) server и adb (Apache-2.0).
Сторонние компоненты: `THIRD_PARTY_NOTICES.md`. Автор: desNotto.

---

## English

OneTakeLink is a Windows 10/11 x64 application that receives video from an
iPhone camera (via the **LensLink Camera** app) or an Android phone (over USB
debugging, nothing installed on the phone), monitors it, records it to MP4 and
manages the recordings in a media library with a built-in player. Russian and
English interface. Version: **0.3**.

### Features

**iPhone connection**
- USB (Apple Mobile Device Service) or Wi-Fi (Bonjour discovery, manual IP).
- Remote camera start on the iPhone; requests 1080p 60 fps H.264 on connect.

**Android connection** (Android 12+, USB)
- Via adb and the scrcpy server shipped with the app; nothing is installed on the phone.
- Camera choice (back / front / external), mode (720p–4K, 30/60 fps), bitrate, H.264/H.265.
- Falls back to 30 fps automatically if the phone cannot do 60 fps in the chosen mode.

**Common**
- Automatic reconnection; connection status with troubleshooting hints.

**Monitoring**
- OpenGL video output, D3D11VA / DXVA2 hardware decoding with software fallback.
- Actual stream parameters: resolution, FPS, codec, bitrate, dropped frames, latency, decoder.
- Overlays: rule of thirds, centre lines, 90 % safe area, 16:9 / 9:16 / 4:5 / 1:1 crop frame. Never written to files.
- Social guidelines for 9:16: Instagram Reels and YouTube Shorts safe zones (red/orange: the app interface covers the video; green: room for faces, text, logos). Approximate, based on Meta and Google guidance for 1080×1920.
- 90° frame rotation (buttons in the monitor corner, R / Shift+R) for a phone mounted sideways or upside down. The rotation is stored in the MP4 as orientation metadata — no re-encoding; players, editors and social networks show the video rotated.
- Full-screen monitor.

**iPhone camera control** (only parameters reported by LensLink Camera)
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
- Top bar: frame format, social guidelines, language (RU / EN; defaults to the system language).

### Requirements

- Windows 10 or 11, x64.
- iPhone: LensLink Camera (App Store). USB needs iTunes or Apple Devices; Wi-Fi needs the iPhone and PC on the same network and OneTakeLink allowed in Windows Firewall.
- Android: version 12 or newer, "USB debugging" enabled (Settings → About phone → tap "Build number" 7 times → Developer options → USB debugging), the debugging prompt confirmed on the phone. Some models need the manufacturer's USB driver.

### Installation and start

1. Download `OneTakeLink-windows-x64`: GitHub → Actions → latest successful "Build" run → Artifacts.
2. Unpack and run `OneTakeLink.exe`. No installation needed.
3. iPhone: open LensLink Camera. Android: connect the cable with USB debugging on.
4. Pick the device in the Source panel and click Connect.

### Keyboard shortcuts

| Key | Action |
|---|---|
| Space | REC / STOP |
| F11, double-click on monitor | Full-screen monitor |
| Esc | Leave full screen |
| R / Shift+R | Rotate the picture 90° / reset rotation |
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
| scrcpy server on Android | `/data/local/tmp/scrcpy-server.jar` | removed from the phone after disconnect |
| adb background server on the PC | `adb.exe` process | stopped on exit if OneTakeLink started it |

Network connections are made only to the iPhone. No telemetry.

### Diagnostics: `otl-cli`

```bat
otl-cli devices                      :: list devices (iPhone and Android)
otl-cli monitor usb 600              :: stream statistics every second for 600 s
otl-cli monitor android 600          :: same for Android
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
| — Android (scrcpy), Reels / Shorts safe zones, RU/EN interface | done |
| 5. Manual ISO/shutter, settings window | planned |
| 6. Compatibility testing, installer | planned |

### Building from source

Requires CMake ≥ 3.21, Qt ≥ 6.4 (Widgets, OpenGLWidgets), FFmpeg (avcodec, avformat, avutil, swscale), Visual Studio 2022.

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64 ^
      -DFFMPEG_ROOT=C:\ffmpeg-7.1.1-full_build-shared
cmake --build build
```

Emulators for testing without phones: LensLink Camera — `python tools/fake_phone.py`, then `OneTakeLink --connect=wifi:127.0.0.1`; Android — `OTL_ADB=tools/fake_adb.py OTL_SCRCPY_SERVER=<any file> OneTakeLink --connect=android:`.
Translations: `tools/update_translations.sh` (source strings are Russian; English in `translations/onetakelink_en.ts`).

### License

GNU GPL v3 or later (`LICENSE`). The iPhone connection module is based on
[LensLink](https://github.com/MyNamesEMurray/LensLink) (GPL-2.0-or-later);
Android uses the [scrcpy](https://github.com/Genymobile/scrcpy) server and adb (Apache-2.0).
Third-party components: `THIRD_PARTY_NOTICES.md`. Author: desNotto.
