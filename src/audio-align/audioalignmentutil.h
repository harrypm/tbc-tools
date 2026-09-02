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
// Reads an explicit RF *source* capture sample rate (Hz) from the metadata
// JSON's videoParameters, if the metadata carries a forward-compatible
// RF-source field (rfSourceSampleRate / rfSourceSampleRateHz / rfSourceFreq /
// rfSampleRate). Returns 0 when no such field is present. NEVER falls back to
// the decoded videoParameters.sampleRate — that is the .tbc format rate, not
// the source RF timebase AAA aligns against (they differ on resampled
// captures). The dialog uses this to auto-set the RF Video Sample Rate field
// only when the metadata explicitly provides it; otherwise the 40 MHz default
// is kept as a user-provided value.
quint32 detectRfSourceSampleRateFromJson(const QString &jsonFilename);
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
                    QString *errorMessage = nullptr,
                    bool convertMonoToStereo = true);
}

#endif // AUDIOALIGNMENTUTIL_H
