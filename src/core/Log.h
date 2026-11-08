// SPDX-License-Identifier: GPL-3.0-or-later
// Local log file (nothing is ever sent anywhere): helps diagnose
// connection and recording problems. %LOCALAPPDATA%/OneTakeLink/OneTakeLink/logs.
#pragma once

#include <QString>

namespace otl {
void installLogging();
QString logFilePath();
} // namespace otl
