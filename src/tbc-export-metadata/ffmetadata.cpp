/************************************************************************

    ffmetadata.cpp

    tbc-export-metadata - Export ld-decode metadata into other formats
    Copyright (C) 2019-2023 Adam Sampson

    This file is part of tbc-tools.

    tbc-export-metadata is free software: you can redistribute it and/or
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

#include "ffmetadata.h"

#include "navigation.h"
#include "vitcdecoder.h"

#include <QtGlobal>
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>
#include <vector>

#include "tbc/logging.h"

namespace {

struct FfmetadataChapter {
    qint32 startField = 0;
    qint32 endFieldExclusive = 0;
    QString title;
    QString comment;
};

QString escapeFfmetadataValue(const QString &value)
{
    QString sanitized = value;
    sanitized.replace(QRegularExpression(QStringLiteral("[\\r\\n]+")), QStringLiteral(" "));

    QString escaped;
    escaped.reserve(sanitized.size() * 2);
    for (const QChar c : sanitized) {
        if (c == QLatin1Char('\\')
            || c == QLatin1Char(';')
            || c == QLatin1Char('#')
            || c == QLatin1Char('=')) {
            escaped.append(QLatin1Char('\\'));
        }
        escaped.append(c);
    }
    return escaped;
}

QString formatVitcTimecode(const VitcDecoder::Vitc &vitc)
{
    const QChar separator = vitc.isDropFrame ? QLatin1Char(';') : QLatin1Char(':');
    return QStringLiteral("%1:%2:%3%4%5")
        .arg(vitc.hour, 2, 10, QLatin1Char('0'))
        .arg(vitc.minute, 2, 10, QLatin1Char('0'))
        .arg(vitc.second, 2, 10, QLatin1Char('0'))
        .arg(separator)
        .arg(vitc.frame, 2, 10, QLatin1Char('0'));
}

QString firstValidVitcTimecodeInRange(TbcMetaData &metaData,
                                      qint32 exportStartField,
                                      qint32 exportEndFieldExclusive)
{
    const VideoSystem system = metaData.getVideoParameters().system;
    for (qint32 field = exportStartField; field < exportEndFieldExclusive; ++field) {
        const TbcMetaData::Vitc &fieldVitc = metaData.getFieldVitc(field + 1);
        if (!fieldVitc.inUse) {
            continue;
        }
        const VitcDecoder::Vitc decoded = VitcDecoder::decode(fieldVitc.vitcData, system);
        if (!decoded.isValid) {
            continue;
        }
        return formatVitcTimecode(decoded);
    }
    return QString();
}

bool getFieldRangeForFrames(TbcMetaData &metaData,
                            qint32 startFrameOneBased,
                            qint32 lengthFrames,
                            qint32 *startField,
                            qint32 *endFieldExclusive)
{
    if (!startField || !endFieldExclusive) {
        return false;
    }

    const qint32 totalFrames = metaData.getNumberOfFrames();
    const qint32 totalFields = metaData.getVideoParameters().numberOfSequentialFields;
    if (totalFrames < 1 || totalFields < 1) {
        return false;
    }

    const qint32 resolvedStartFrame = startFrameOneBased > 0 ? startFrameOneBased : 1;
    if (resolvedStartFrame < 1 || resolvedStartFrame > totalFrames) {
        return false;
    }

    const qint32 maxLength = totalFrames - resolvedStartFrame + 1;
    if (maxLength < 1) {
        return false;
    }
    const qint32 resolvedLength = lengthFrames > 0 ? qMin(lengthFrames, maxLength) : maxLength;
    if (resolvedLength < 1) {
        return false;
    }

    const qint32 resolvedEndFrame = resolvedStartFrame + resolvedLength - 1;
    const qint32 startFieldOneBased = metaData.getFirstFieldNumber(resolvedStartFrame);
    const qint32 endFieldOneBased = metaData.getSecondFieldNumber(resolvedEndFrame);
    if (startFieldOneBased < 1 || endFieldOneBased < 1) {
        return false;
    }

    const qint32 resolvedStartField = qMax<qint32>(0, startFieldOneBased - 1);
    const qint32 resolvedEndFieldExclusive = qMin<qint32>(totalFields, endFieldOneBased);
    if (resolvedStartField >= resolvedEndFieldExclusive) {
        return false;
    }

    *startField = resolvedStartField;
    *endFieldExclusive = resolvedEndFieldExclusive;
    return true;
}

bool clipChapterToRange(const FfmetadataChapter &source,
                        qint32 clipStartField,
                        qint32 clipEndFieldExclusive,
                        FfmetadataChapter *result)
{
    if (!result) {
        return false;
    }
    result->startField = qMax(source.startField, clipStartField);
    result->endFieldExclusive = qMin(source.endFieldExclusive, clipEndFieldExclusive);
    result->title = source.title;
    result->comment = source.comment;
    return result->endFieldExclusive > result->startField;
}

void insertMarkerChapterWithoutOverlap(std::vector<FfmetadataChapter> *chapters,
                                       const FfmetadataChapter &markerChapter)
{
    if (!chapters) {
        return;
    }
    if (markerChapter.endFieldExclusive <= markerChapter.startField) {
        return;
    }

    if (chapters->empty()) {
        chapters->push_back(markerChapter);
        return;
    }

    std::vector<FfmetadataChapter> merged;
    merged.reserve(chapters->size() + 2);

    bool inserted = false;
    for (const auto &chapter : *chapters) {
        if (markerChapter.endFieldExclusive <= chapter.startField) {
            if (!inserted) {
                merged.push_back(markerChapter);
                inserted = true;
            }
            merged.push_back(chapter);
            continue;
        }
        if (markerChapter.startField >= chapter.endFieldExclusive) {
            merged.push_back(chapter);
            continue;
        }

        if (chapter.startField < markerChapter.startField) {
            FfmetadataChapter head = chapter;
            head.endFieldExclusive = markerChapter.startField;
            if (head.endFieldExclusive > head.startField) {
                merged.push_back(head);
            }
        }

        if (!inserted) {
            merged.push_back(markerChapter);
            inserted = true;
        }

        if (markerChapter.endFieldExclusive < chapter.endFieldExclusive) {
            FfmetadataChapter tail = chapter;
            tail.startField = markerChapter.endFieldExclusive;
            if (tail.endFieldExclusive > tail.startField) {
                merged.push_back(tail);
            }
        }
    }

    if (!inserted) {
        merged.push_back(markerChapter);
    }

    *chapters = std::move(merged);
}

} // namespace

bool writeFfmetadata(TbcMetaData &metaData,
                     const QString &fileName,
                     qint32 startFrameOneBased,
                     qint32 lengthFrames,
                     bool includeVitcTimecode)
{
    const auto videoParameters = metaData.getVideoParameters();

    // Select the appropriate timebase to make 0-based field numbers work
    const QString timeBase = videoParameters.system == PAL ? "1/50" : "1001/60000";

    qint32 exportStartField = 0;
    qint32 exportEndFieldExclusive = 0;
    if (!getFieldRangeForFrames(metaData,
                                startFrameOneBased,
                                lengthFrames,
                                &exportStartField,
                                &exportEndFieldExclusive)) {
        tbcDebug(QStringLiteral("writeFfmetadata: Could not resolve export start/length range"));
        return false;
    }
    const QString vitcTimecode = includeVitcTimecode
                                     ? firstValidVitcTimecodeInRange(metaData,
                                                                     exportStartField,
                                                                     exportEndFieldExclusive)
                                     : QString();

    // Extract navigation information
    const NavigationInfo navInfo(metaData);
    std::vector<FfmetadataChapter> chapters;
    chapters.reserve(navInfo.chapters.size() + 1);

    for (const auto &chapter : navInfo.chapters) {
        FfmetadataChapter candidate;
        candidate.startField = chapter.startField;
        candidate.endFieldExclusive = chapter.endField;
        candidate.title = QStringLiteral("Chapter %1").arg(chapter.number);
        FfmetadataChapter clipped;
        if (clipChapterToRange(candidate, exportStartField, exportEndFieldExclusive, &clipped)) {
            chapters.push_back(clipped);
        }
    }

    if (videoParameters.userMarkerSelection > 0) {
        const qint32 markerFrame = videoParameters.userMarkerSelection;
        qint32 markerStartField = 0;
        qint32 markerEndFieldExclusive = 0;
        if (getFieldRangeForFrames(metaData,
                                   markerFrame,
                                   1,
                                   &markerStartField,
                                   &markerEndFieldExclusive)) {
            FfmetadataChapter markerChapter;
            markerChapter.startField = markerStartField;
            markerChapter.endFieldExclusive = markerEndFieldExclusive;
            markerChapter.comment = videoParameters.userMarkerComment.trimmed();
            markerChapter.title = markerChapter.comment.isEmpty()
                                      ? QStringLiteral("User Marker (%1)").arg(markerFrame)
                                      : markerChapter.comment;
            FfmetadataChapter clippedMarker;
            if (clipChapterToRange(markerChapter,
                                   exportStartField,
                                   exportEndFieldExclusive,
                                   &clippedMarker)) {
                insertMarkerChapterWithoutOverlap(&chapters, clippedMarker);
            }
        }
    }

    // Open the output file
    QFile file(fileName);
    if (!file.open(QFile::WriteOnly | QFile::Text)) {
        tbcDebug(QStringLiteral("writeFfmetadata: Could not open file for output"));
        return false;
    }
    QTextStream stream(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    stream.setCodec("UTF-8");
#endif

    // Write the header
    stream << ";FFMETADATA1\n";
    if (!vitcTimecode.isEmpty()) {
        stream << "timecode=" << escapeFfmetadataValue(vitcTimecode) << "\n";
    }

    // Write the chapter changes
    for (const auto &chapter : chapters) {
        stream << "\n";
        stream << "[CHAPTER]\n";
        stream << "TIMEBASE=" << timeBase << "\n";
        stream << "START=" << (chapter.startField - exportStartField) << "\n";
        stream << "END=" << (chapter.endFieldExclusive - exportStartField - 1) << "\n";
        stream << "title=" << escapeFfmetadataValue(chapter.title) << "\n";
        if (!chapter.comment.isEmpty()) {
            stream << "comment=" << escapeFfmetadataValue(chapter.comment) << "\n";
        }
    }

    if (!navInfo.stopCodes.empty()) {
        // Write the stop codes, as comments
        // XXX Is there a way to represent these properly?
        stream << "\n";
        for (qint32 field : navInfo.stopCodes) {
            if (field < exportStartField || field >= exportEndFieldExclusive) {
                continue;
            }
            stream << "; Stop code at " << (field - exportStartField) << "\n";
        }
    }

    // Done!
    file.close();
    return true;
}
