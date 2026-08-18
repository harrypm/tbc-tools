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
