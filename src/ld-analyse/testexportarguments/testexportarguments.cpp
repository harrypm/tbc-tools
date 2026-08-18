/************************************************************************

    testexportarguments.cpp

    Unit tests for export argument helpers
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

#include <cassert>
#include <iostream>
#include <QDir>
#include <QFileInfo>

#include "exportarguments.h"

using std::cerr;

void testDropoutDisablePolicy()
{
    cerr << "Testing ExportArguments::shouldDisableDropoutCorrection\n";

    // Non-disabled modes should remain enabled regardless of range start.
    assert(!ExportArguments::shouldDisableDropoutCorrection(QStringLiteral("basic"), 1));
    assert(!ExportArguments::shouldDisableDropoutCorrection(QStringLiteral("basic"), 250));
    assert(!ExportArguments::shouldDisableDropoutCorrection(QStringLiteral("heavy"), 1));
    assert(!ExportArguments::shouldDisableDropoutCorrection(QStringLiteral("heavy"), 250));

    // Disabled mode must always disable dropout correction.
    assert(ExportArguments::shouldDisableDropoutCorrection(QStringLiteral("disabled"), 1));
    assert(ExportArguments::shouldDisableDropoutCorrection(QStringLiteral("disabled"), 250));
}
void testDefaultActiveAreaFramingPolicy()
{
    cerr << "Testing ExportArguments::isDefaultActiveAreaFraming\n";

    assert(ExportArguments::isDefaultActiveAreaFraming(QStringLiteral("active_area"), false));
    assert(!ExportArguments::isDefaultActiveAreaFraming(QStringLiteral("active_area"), true));
    assert(!ExportArguments::isDefaultActiveAreaFraming(QStringLiteral("active_vbi"), false));
    assert(!ExportArguments::isDefaultActiveAreaFraming(QStringLiteral("user_defined"), false));
}

void testSanitizeOutputBasePath()
{
    cerr << "Testing ExportArguments::sanitizeOutputBasePath\n";

    const QString tempDir = QDir::tempPath();
    const QString mkvPath = QDir(tempDir).filePath(QStringLiteral("capture.side_a.mkv"));
    const QString preservedPath = QDir(tempDir).filePath(QStringLiteral("capture.side_a.final"));
    const QString mp4Path = QDir(tempDir).filePath(QStringLiteral("capture.side_a.MP4"));
    const QString hiddenLikePath = QDir(tempDir).filePath(QStringLiteral(".capture"));

    const QString sanitizedMkvPath = ExportArguments::sanitizeOutputBasePath(mkvPath);
    const QString sanitizedPreservedPath = ExportArguments::sanitizeOutputBasePath(preservedPath);
    const QString sanitizedMp4Path = ExportArguments::sanitizeOutputBasePath(mp4Path);
    const QString sanitizedHiddenPath = ExportArguments::sanitizeOutputBasePath(hiddenLikePath);

    assert(QFileInfo(sanitizedMkvPath).fileName() == QStringLiteral("capture.side_a"));
    assert(QFileInfo(sanitizedPreservedPath).fileName() == QStringLiteral("capture.side_a.final"));
    assert(QFileInfo(sanitizedMp4Path).fileName() == QStringLiteral("capture.side_a"));
    assert(QFileInfo(sanitizedHiddenPath).fileName() == QStringLiteral(".capture"));
}

int main()
{
    testDropoutDisablePolicy();
    testDefaultActiveAreaFramingPolicy();
    testSanitizeOutputBasePath();
    return 0;
}
