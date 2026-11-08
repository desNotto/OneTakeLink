# OneTakeLink

[Русский](#русский) · [English](#english)

---

## Русский

Приложение для Windows 10/11 x64. Принимает видео с камеры iPhone или Android-телефона, показывает его на мониторе и записывает дубли в MP4. Версия 1.0.0.

### Возможности

- Источники: iPhone через приложение LensLink Camera (USB или Wi-Fi); Android 12+ по USB через отладку (adb и сервер scrcpy входят в комплект, на телефон ничего не устанавливается).
- Запись в MP4 без перекодирования, без звука. Восстановление дубля после сбоя; сохранение записанного фрагмента при обрыве связи; контроль свободного места.
- Монитор: фактические разрешение, FPS, битрейт, пропуски кадров, задержка.
- Рамка формата 16:9 / 9:16 / 4:5 / 1:1, безопасные зоны Instagram Reels и YouTube Shorts, сетка третей. В файл не записываются; записывается полный кадр.
- Поворот кадра на 90°. Сохраняется в MP4 как метка ориентации.
- Настройки камеры: iPhone — режим, объектив, зум, экспокоррекция, фиксация фокуса, экспозиции и баланса белого; Android — камера, режим, битрейт, кодек.
- Медиатека: миниатюры или таблица, поиск, фильтры, сортировка, встроенный плеер, избранное, переименование, удаление в Корзину.
- Интерфейс на русском и английском.

### Требования

- Windows 10 или 11, x64.
- iPhone: приложение LensLink Camera (App Store); для USB — iTunes или Apple Devices.
- Android: версия 12 или новее, включённая отладка по USB.

### Установка

Скачать на странице [Releases](https://github.com/desNotto/onetakelink/releases/latest):

- `OneTakeLink-<версия>-setup-windows-x64.exe` — установщик;
- `OneTakeLink-<версия>-windows-x64.zip` — портативная версия, запуск `OneTakeLink.exe` без установки.

Файлы не подписаны цифровой подписью; при предупреждении SmartScreen: «Подробнее» → «Выполнить в любом случае».

### Как пользоваться

1. Подготовить телефон:
   - iPhone — открыть LensLink Camera; для USB подключить кабель и подтвердить «Доверять этому компьютеру»;
   - Android — включить отладку по USB (Настройки → О телефоне → 7 раз «Номер сборки» → Для разработчиков → Отладка по USB), подключить кабель, подтвердить запрос отладки.
2. В панели «Источник» нажать «Найти», выбрать устройство, нажать «Подключить».
3. При необходимости: выбрать формат на верхней панели, включить безопасные зоны Instagram или YouTube, сетку третей, повернуть кадр кнопкой в углу монитора.
4. Указать папку и проект в панели «Запись».
5. Нажать REC (Space). Остановить — Space.
6. Дубль появляется в медиатеке и открывается в плеере.

### Горячие клавиши

| Клавиша | Действие |
|---|---|
| Space | REC / STOP |
| R / Shift+R | Повернуть кадр на 90° / сбросить поворот |
| G | Сетка третей |
| F11 / Esc | Полноэкранный монитор / выход |
| K | Плеер: воспроизведение / пауза |
| `,` / `.` | Плеер: кадр назад / вперёд |
| F2 | Переименовать дубль |
| F | Избранное |
| Delete | Удалить в Корзину |
| F5 | Обновить медиатеку |

### Данные на диске

| Что | Где | Очистка |
|---|---|---|
| Записи | папка из настроек, по умолчанию `Видео\OneTakeLink` | только пользователем |
| Настройки | `%LOCALAPPDATA%\OneTakeLink\OneTakeLink\settings.ini` | при удалении программы |
| Временный файл `*.recording.mp4` | рядом с дублем | после остановки записи; после сбоя восстанавливается при запуске |
| Кэш миниатюр | `%LOCALAPPDATA%\OneTakeLink\OneTakeLink\cache\thumbs` | вместе с дублем; при запуске — старше 60 дней и сверх 256 МБ; при удалении программы |
| Журнал | `%LOCALAPPDATA%\OneTakeLink\OneTakeLink\logs` | 2 последних сеанса, до 10 МБ каждый; при удалении программы |
| Сервер scrcpy на Android | `/data/local/tmp/scrcpy-server.jar` | после отключения телефона |
| Фоновый сервер adb | процесс `adb.exe` | при выходе, если его запустил OneTakeLink |

Сетевые соединения — только с телефоном. Телеметрии нет.

### Прочее

- Диагностика подключения: `otl-cli devices`, `otl-cli monitor usb|android <секунды>`, `otl-cli recover <папка>`.
- Сборка из исходников и устройство проекта: `CLAUDE.md`.
- Лицензия: GNU GPL v3 или новее. Используются [LensLink](https://github.com/MyNamesEMurray/LensLink) (GPL-2.0-or-later), [scrcpy](https://github.com/Genymobile/scrcpy) и adb (Apache-2.0), Qt (LGPLv3), FFmpeg (GPL); подробнее — `THIRD_PARTY_NOTICES.md`.
- Автор: desNotto.

---

## English

Windows 10/11 x64 application. Receives video from an iPhone or Android phone camera, shows it on a monitor and records takes to MP4. Version 1.0.0.

### Features

- Sources: iPhone via the LensLink Camera app (USB or Wi-Fi); Android 12+ over USB debugging (adb and the scrcpy server are bundled, nothing is installed on the phone).
- MP4 recording without re-encoding, no audio. Take recovery after a crash; the recorded part is kept on connection loss; free-space checks.
- Monitor: actual resolution, FPS, bitrate, dropped frames, latency.
- 16:9 / 9:16 / 4:5 / 1:1 format frame, Instagram Reels and YouTube Shorts safe zones, rule-of-thirds grid. Not written to files; the full frame is recorded.
- 90° frame rotation. Stored in the MP4 as orientation metadata.
- Camera settings: iPhone — mode, lens, zoom, exposure compensation, focus/exposure/white-balance lock; Android — camera, mode, bitrate, codec.
- Media library: thumbnails or table, search, filters, sorting, built-in player, favourites, rename, delete to Recycle Bin.
- Russian and English interface.

### Requirements

- Windows 10 or 11, x64.
- iPhone: LensLink Camera app (App Store); USB needs iTunes or Apple Devices.
- Android: version 12 or newer, USB debugging enabled.

### Installation

Download from [Releases](https://github.com/desNotto/onetakelink/releases/latest):

- `OneTakeLink-<version>-setup-windows-x64.exe` — installer;
- `OneTakeLink-<version>-windows-x64.zip` — portable, run `OneTakeLink.exe` without installing.

The files are not code-signed; on a SmartScreen warning: "More info" → "Run anyway".

### How to use

1. Prepare the phone:
   - iPhone — open LensLink Camera; for USB, connect the cable and confirm "Trust This Computer";
   - Android — enable USB debugging (Settings → About phone → tap "Build number" 7 times → Developer options → USB debugging), connect the cable, confirm the debugging prompt.
2. In the Source panel click Find, select the device, click Connect.
3. Optionally: choose the format in the top bar, turn on Instagram or YouTube safe zones or the thirds grid, rotate the picture with the button in the monitor corner.
4. Set the folder and project in the Recording panel.
5. Press REC (Space). Stop with Space.
6. The take appears in the media library and opens in the player.

### Keyboard shortcuts

| Key | Action |
|---|---|
| Space | REC / STOP |
| R / Shift+R | Rotate picture 90° / reset rotation |
| G | Rule-of-thirds grid |
| F11 / Esc | Full-screen monitor / leave |
| K | Player: play / pause |
| `,` / `.` | Player: previous / next frame |
| F2 | Rename take |
| F | Favourite |
| Delete | Delete to Recycle Bin |
| F5 | Refresh media library |

### Data on disk

| What | Where | Cleanup |
|---|---|---|
| Recordings | folder from settings, default `Videos\OneTakeLink` | by the user only |
| Settings | `%LOCALAPPDATA%\OneTakeLink\OneTakeLink\settings.ini` | on uninstall |
| Temporary `*.recording.mp4` | next to the take | when recording stops; recovered on start after a crash |
| Thumbnail cache | `%LOCALAPPDATA%\OneTakeLink\OneTakeLink\cache\thumbs` | with the take; on start — older than 60 days and above 256 MB; on uninstall |
| Log | `%LOCALAPPDATA%\OneTakeLink\OneTakeLink\logs` | last 2 sessions, up to 10 MB each; on uninstall |
| scrcpy server on Android | `/data/local/tmp/scrcpy-server.jar` | after the phone disconnects |
| adb background server | `adb.exe` process | on exit, if OneTakeLink started it |

Network connections are made only to the phone. No telemetry.

### Other

- Connection diagnostics: `otl-cli devices`, `otl-cli monitor usb|android <seconds>`, `otl-cli recover <folder>`.
- Building from source and project layout: `CLAUDE.md`.
- License: GNU GPL v3 or later. Uses [LensLink](https://github.com/MyNamesEMurray/LensLink) (GPL-2.0-or-later), [scrcpy](https://github.com/Genymobile/scrcpy) and adb (Apache-2.0), Qt (LGPLv3), FFmpeg (GPL); see `THIRD_PARTY_NOTICES.md`.
- Author: desNotto.
