/************************************************************************

    vitcffmetadata.cpp

    tbc-export-metadata - Export ld-decode metadata into other formats
    Copyright (C) 2026 Simon Inns

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

#include "vitcffmetadata.h"

#include "vitcdecoder.h"

#include <QtGlobal>
#include <QFile>
#include <QTextStream>

#include "tbc/logging.h"

namespace {
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

QString formatPtsTime(qint32 frameIndex, bool is30Frame)
{
    const double fps = is30Frame ? (30000.0 / 1001.0) : 25.0;
    QString ptsTime = QString::number(static_cast<double>(frameIndex) / fps, 'f', 6);

    while (ptsTime.endsWith(QLatin1Char('0'))) {
        ptsTime.chop(1);
    }
    if (ptsTime.endsWith(QLatin1Char('.'))) {
        ptsTime.chop(1);
    }
    if (ptsTime.isEmpty()) {
        return QStringLiteral("0");
    }
    return ptsTime;
}

bool getFrameVitcTimecode(const LdDecodeMetaData &metaData,
                          qint32 frameNumberOneBased,
                          QString *timecode)
{
    if (!timecode) {
        return false;
    }
    *timecode = QString();

    const qint32 firstField = metaData.getFirstFieldNumber(frameNumberOneBased);
    const qint32 secondField = metaData.getSecondFieldNumber(frameNumberOneBased);
    const qint32 numberOfFields = metaData.getNumberOfFields();
    const VideoSystem system = metaData.getVideoParameters().system;

    const std::array<qint32, 2> frameFields{firstField, secondField};
    for (qint32 fieldNumber : frameFields) {
        if (fieldNumber < 1 || fieldNumber > numberOfFields) {
            continue;
        }
        const LdDecodeMetaData::Vitc &fieldVitc = metaData.getFieldVitc(fieldNumber);
        if (!fieldVitc.inUse) {
            continue;
        }
        const VitcDecoder::Vitc decoded = VitcDecoder::decode(fieldVitc.vitcData, system);
        if (!decoded.isValid) {
            continue;
        }
        *timecode = formatVitcTimecode(decoded);
        return true;
    }

    return false;
}
} // namespace

bool writeVitcFfmetadataText(LdDecodeMetaData &metaData, const QString &fileName)
{
    QFile file(fileName);
    if (!file.open(QFile::WriteOnly | QFile::Text)) {
        tbcDebug(QStringLiteral("writeVitcFfmetadataText: Could not open file for output"));
        return false;
    }
    QTextStream stream(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    stream.setCodec("UTF-8");
#endif

    const bool is30Frame = (metaData.getVideoParameters().system != PAL);
    const qint32 numberOfFrames = qMax<qint32>(0, metaData.getNumberOfFrames());

    for (qint32 frameIndex = 0; frameIndex < numberOfFrames; ++frameIndex) {
        QString frameVitcTimecode;
        const bool hasVitc = getFrameVitcTimecode(metaData, frameIndex + 1, &frameVitcTimecode);

        stream << QStringLiteral("frame:%1 pts:%2 pts_time:%3\n")
                      .arg(frameIndex, -5)
                      .arg(frameIndex, -7)
                      .arg(formatPtsTime(frameIndex, is30Frame));
        stream << QStringLiteral("lavfi.readvitc.found=%1\n").arg(hasVitc ? 1 : 0);
        if (hasVitc) {
            stream << QStringLiteral("lavfi.readvitc.tc_str=%1\n").arg(frameVitcTimecode);
        }
    }

    file.close();
    return true;
}
