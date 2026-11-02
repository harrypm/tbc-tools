/************************************************************************

    testthemeconfiguration.cpp

    Headless round-trip test for the Themes-menu theme persistence keys in
    Configuration (theme.keepChoice + theme.mode). Runs against Qt's
    test-mode configuration sandbox (QStandardPaths::setTestModeEnabled), so
    it never touches the user's real tbc-analyse.ini.

    Uses explicit verify() checks instead of assert(): the test target builds
    Release (-DNDEBUG), where assert() is compiled out.

    Copyright (C) 2026 Simon Inns

    This file is part of tbc-tools.

    tbc-tools is free software: you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

************************************************************************/

#include <iostream>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include "configuration.h"

using std::cerr;

static bool g_failed = false;

static void verify(bool condition, const QString &what)
{
    if (!condition) {
        cerr << "FAIL: " << what.toStdString() << "\n";
        g_failed = true;
    }
}

int main(int argc, char *argv[])
{
    // Sandbox all QSettings/QStandardPaths access away from the user's real
    // configuration; must be set before the paths are first resolved.
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("testthemeconfiguration"));

    // Start from a clean test-sandbox config file.
    const QString testIniPath = QDir(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation))
                                    .filePath(QStringLiteral("tbc-analyse.ini"));
    QFile::remove(testIniPath);

    // Defaults for a fresh (or pre-theme-key) configuration: do not keep the
    // choice, follow the OS scheme.
    {
        Configuration config;
        verify(!config.getKeepThemeChoice(),
               QStringLiteral("fresh config keepChoice should default to false"));
        verify(config.getThemeMode() == QStringLiteral("stock-system"),
               QStringLiteral("fresh config mode should default to stock-system, got %1")
                   .arg(config.getThemeMode()));
    }

    // Round-trip: write a kept choice, then re-read with a fresh instance.
    {
        Configuration config;
        config.setKeepThemeChoice(true);
        config.setThemeMode(QStringLiteral("stock-light"));
        config.writeConfiguration();
    }
    {
        Configuration config;
        verify(config.getKeepThemeChoice(),
               QStringLiteral("keepChoice=true did not survive a write/read round trip"));
        verify(config.getThemeMode() == QStringLiteral("stock-light"),
               QStringLiteral("mode=stock-light did not survive a write/read round trip, got %1")
                   .arg(config.getThemeMode()));
    }

    // Clearing the choice resets the stored mode to the follow-OS default
    // (the uncheck path in the Themes menu does exactly this).
    {
        Configuration config;
        config.setKeepThemeChoice(false);
        config.setThemeMode(QStringLiteral("stock-system"));
        config.writeConfiguration();
    }
    {
        Configuration config;
        verify(!config.getKeepThemeChoice(),
               QStringLiteral("keepChoice=false did not survive a write/read round trip"));
        verify(config.getThemeMode() == QStringLiteral("stock-system"),
               QStringLiteral("mode should be back to stock-system, got %1")
                   .arg(config.getThemeMode()));
    }

    // Sanitisation: anything that is not a stock preset (including garbage
    // from a hand-edited ini) reads back as stock-system so startup follows
    // the OS instead of an unknown mode.
    {
        Configuration config;
        config.setKeepThemeChoice(true);
        config.setThemeMode(QStringLiteral("Fusion"));
        config.writeConfiguration();
    }
    {
        Configuration config;
        verify(config.getThemeMode() == QStringLiteral("stock-system"),
               QStringLiteral("non-stock mode should sanitise to stock-system, got %1")
                   .arg(config.getThemeMode()));
    }

    QFile::remove(testIniPath);

    if (g_failed) {
        cerr << "testthemeconfiguration FAILED\n";
        return 1;
    }
    cerr << "testthemeconfiguration passed\n";
    return 0;
}
