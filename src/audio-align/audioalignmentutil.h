/******************************************************************************
 * audioalignmentutil.h
 * tbc-audio-align - Audio alignment helper for ld-decode
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#ifndef AUDIOALIGNMENTUTIL_H
#define AUDIOALIGNMENTUTIL_H
#include <functional>

#include <QString>
#include <QtGlobal>

namespace AudioAlignmentUtil {
using ProgressCallback = std::function<void(int percent, const QString &statusMessage)>;
using CancelCallback = std::function<bool()>;
QString normalizePathForCurrentPlatform(const QString &path);
QString defaultAlignedOutputPath(const QString &inputFilename);
QString autoDetectInputAudioFile(const QString &jsonFilename, const QString &excludeFile = QString());
QString autoDetectLinearInputAudioFile(const QString &jsonFilename, const QString &excludeFile = QString());
QString autoDetectHifiInputAudioFile(const QString &jsonFilename, const QString &excludeFile = QString());
// Resolves the VhsDecodeAutoAudioAlign executable path the same way
// runStreamAlign does (bundled vendor dir relative to the application dir,
// then PATH). Returns the executable path, or an empty string if not found
// (with errorMessage set). This is detection only — it does not check that a
// launcher (mono/env) is available. Exposed so CI/detection tests can verify
// ld-analyse actually finds the bundled AAA without driving the full pipeline.
QString resolvedAudioAlignPath(QString *errorMessage = nullptr);
// Builds the argv to launch the resolved AAA executable (program first, then
// any prefix arguments — e.g. ["env","APPIMAGE_EXTRACT_AND_RUN=1",<appimage>]
// for the self-contained AppImage, or ["mono",<exe>] for the .exe on
// non-Windows). Returns an empty list if AAA is not found or its launcher
// (mono for the .exe) is unavailable (errorMessage set). Append the tool's
// own arguments (e.g. "show-build-info") to the returned argv to run it.
QStringList audioAlignRunnerCommand(QString *errorMessage = nullptr);
bool runStreamAlign(const QString &jsonFilename,
                    const QString &inputFilename,
                    const QString &outputFilename,
                    quint32 rfVideoSampleRateHz,
                    bool overwriteOutput,
                    const ProgressCallback &progressCallback = ProgressCallback(),
                    const CancelCallback &cancelCallback = CancelCallback(),
                    QString *errorMessage = nullptr);
}

#endif // AUDIOALIGNMENTUTIL_H
