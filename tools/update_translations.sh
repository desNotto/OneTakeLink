#!/usr/bin/env bash
# Re-extracts translatable strings (Russian sources) into the English .ts and
# compiles the .qm embedded in the app. Translate new entries (Qt Linguist or
# by editing the .ts), then run this again. Needs Qt's lupdate/lrelease
# (Linux: apt install qt6-l10n-tools).
set -euo pipefail
cd "$(dirname "$0")/.."
BIN=${QT_BIN:-/usr/lib/qt6/bin}
"$BIN/lupdate" -no-obsolete -source-language ru -target-language en src tools -ts translations/onetakelink_en.ts
"$BIN/lrelease" translations/onetakelink_en.ts -qm translations/onetakelink_en.qm
