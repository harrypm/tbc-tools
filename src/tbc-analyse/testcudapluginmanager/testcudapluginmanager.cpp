/************************************************************************

    testcudapluginmanager.cpp

    Regression test for the CUDA runtime plugin release selection. The cache
    repo (harrypm/tbc-tools-ci-cache) hosts several plugin streams
    (cuda-plugin-v* for the CUDA runtime, cuda-lite-plugin-v* for the teletext
    lite runtime); the repo-wide /releases/latest endpoint returns whichever
    stream published most recently, which hid the CUDA runtime plugin once
    cuda-lite-plugin-v1.0.0 became the newest release. The manager now lists
    the releases and selects by tag prefix - this test pins that behaviour.

    Uses explicit verify() checks instead of assert(): the test target builds
    Release (-DNDEBUG), where assert() is compiled out.

    Copyright (C) 2026 Harry Munday

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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include "cudapluginmanager.h"

using std::cerr;

static bool g_failed = false;

static void verify(bool condition, const QString &what)
{
    if (!condition) {
        cerr << "FAIL: " << what.toStdString() << "\n";
        g_failed = true;
    }
}

// Build a minimal GitHub /releases listing entry.
static QJsonObject release(const QString &tagName,
                           bool draft = false,
                           bool prerelease = false)
{
    QJsonObject entry;
    entry.insert(QStringLiteral("tag_name"), tagName);
    entry.insert(QStringLiteral("draft"), draft);
    entry.insert(QStringLiteral("prerelease"), prerelease);
    entry.insert(QStringLiteral("html_url"),
                 QStringLiteral("https://example.com/%1").arg(tagName));
    return entry;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("testcudapluginmanager"));

    // Reproduction of the live failure: cuda-lite-plugin-v1.0.0 is the newest
    // (and GitHub's repo-wide "latest") release; the CUDA runtime plugin is
    // the older cuda-plugin-v1.0.0. Selection must pick the cuda-plugin entry.
    {
        QJsonArray releases;
        releases.append(release(QStringLiteral("cuda-lite-plugin-v1.0.0")));
        releases.append(release(QStringLiteral("cuda-plugin-v1.0.0")));

        const QJsonObject selected = CudaPluginManager::selectLatestCudaPluginRelease(releases);
        verify(!selected.isEmpty(),
               QStringLiteral("no release selected although a cuda-plugin entry exists"));
        verify(selected.value(QStringLiteral("tag_name")).toString()
                   == QStringLiteral("cuda-plugin-v1.0.0"),
               QStringLiteral("selection must match the cuda-plugin tag prefix, not the "
                               "repo-wide latest (got %1)")
                   .arg(selected.value(QStringLiteral("tag_name")).toString()));
    }

    // Newest-first ordering: with multiple cuda-plugin releases, the first
    // (newest) listing entry wins.
    {
        QJsonArray releases;
        releases.append(release(QStringLiteral("cuda-lite-plugin-v2.0.0")));
        releases.append(release(QStringLiteral("cuda-plugin-v2.1.0")));
        releases.append(release(QStringLiteral("cuda-plugin-v1.0.0")));

        const QJsonObject selected = CudaPluginManager::selectLatestCudaPluginRelease(releases);
        verify(selected.value(QStringLiteral("tag_name")).toString()
                   == QStringLiteral("cuda-plugin-v2.1.0"),
               QStringLiteral("expected the newest cuda-plugin entry, got %1")
                   .arg(selected.value(QStringLiteral("tag_name")).toString()));
    }

    // Drafts and prereleases are skipped (mirrors /releases/latest semantics).
    {
        QJsonArray releases;
        releases.append(release(QStringLiteral("cuda-plugin-v3.0.0-rc1"), false, true));
        releases.append(release(QStringLiteral("cuda-plugin-v3.0.0-draft"), true, false));
        releases.append(release(QStringLiteral("cuda-plugin-v2.1.0")));

        const QJsonObject selected = CudaPluginManager::selectLatestCudaPluginRelease(releases);
        verify(selected.value(QStringLiteral("tag_name")).toString()
                   == QStringLiteral("cuda-plugin-v2.1.0"),
               QStringLiteral("draft/prerelease cuda-plugin entries must be skipped, got %1")
                   .arg(selected.value(QStringLiteral("tag_name")).toString()));
    }

    // Tag prefix must not match other plugin streams (cuda-lite-plugin-v*
    // starts with "cuda-" but not "cuda-plugin-v").
    {
        QJsonArray releases;
        releases.append(release(QStringLiteral("cuda-lite-plugin-v9.0.0")));

        const QJsonObject selected = CudaPluginManager::selectLatestCudaPluginRelease(releases);
        verify(selected.isEmpty(),
               QStringLiteral("cuda-lite entries must never be selected as the CUDA runtime "
                              "plugin release"));
    }

    // No releases at all -> empty selection (the manager reports "no CUDA
    // plugin release published").
    {
        const QJsonObject selected = CudaPluginManager::selectLatestCudaPluginRelease(QJsonArray());
        verify(selected.isEmpty(),
               QStringLiteral("empty release listing must select nothing"));
    }

    // Non-object entries in the array are skipped without breaking selection.
    {
        QJsonArray releases;
        releases.append(QJsonValue(42));
        releases.append(release(QStringLiteral("cuda-plugin-v1.0.0")));

        const QJsonObject selected = CudaPluginManager::selectLatestCudaPluginRelease(releases);
        verify(selected.value(QStringLiteral("tag_name")).toString()
                   == QStringLiteral("cuda-plugin-v1.0.0"),
               QStringLiteral("non-object entries must be skipped"));
    }

    if (g_failed) {
        cerr << "testcudapluginmanager FAILED\n";
        return 1;
    }
    cerr << "testcudapluginmanager passed\n";
    return 0;
}
