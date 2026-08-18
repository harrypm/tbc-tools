/************************************************************************

    csv.cpp

    tbc-export-metadata - Export JSON metadata into other formats
    Copyright (C) 2018-2019 Simon Inns

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

#include "csv.h"

#include "vbidecoder.h"

#include <QtGlobal>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMap>
#include <QRegularExpression>
#include <QTextStream>
#include "tbc/logging.h"

// Create an 'escaped string' for safe CSV output of QStrings
static QString escapedString(const QString &unescapedString)
{
    if (!unescapedString.contains(QLatin1Char(',')))
        return unescapedString;
    QString escapedString = unescapedString;
    return '\"' + escapedString.replace(QLatin1Char('\"'), QStringLiteral("\"\"")) + '\"';
}

struct UserMarker
{
    qint32 frame = -1;
    QString comment;
};

qint32 jsonValueToInt(const QJsonValue &value, bool *ok = nullptr)
{
    bool conversionOk = false;
    qint32 output = -1;
    if (value.isDouble()) {
        const double numericValue = value.toDouble();
        output = static_cast<qint32>(numericValue);
        conversionOk = true;
    } else if (value.isString()) {
        output = value.toString().toInt(&conversionOk);
    }
    if (ok) {
        *ok = conversionOk;
    }
    return conversionOk ? output : -1;
}

QVector<UserMarker> normaliseUserMarkers(const QVector<UserMarker> &markers, qint32 totalFrames)
{
    QMap<qint32, QString> notesByFrame;
    const bool clampToKnownRange = (totalFrames > 0);
    for (const UserMarker &marker : markers) {
        if (marker.frame <= 0) {
            continue;
        }
        qint32 frame = marker.frame;
        if (clampToKnownRange) {
            frame = qBound<qint32>(1, frame, totalFrames);
        }
        if (frame <= 0) {
            continue;
        }
        notesByFrame.insert(frame, marker.comment.trimmed());
    }

    QVector<UserMarker> normalizedMarkers;
    normalizedMarkers.reserve(notesByFrame.size());
    for (auto it = notesByFrame.cbegin(); it != notesByFrame.cend(); ++it) {
        normalizedMarkers.append({it.key(), it.value()});
    }
    return normalizedMarkers;
}

QVector<UserMarker> parseUserMarkersJson(const QString &userMarkersJson, qint32 totalFrames)
{
    const QByteArray jsonBytes = userMarkersJson.trimmed().toUtf8();
    if (jsonBytes.isEmpty()) {
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument jsonDocument = QJsonDocument::fromJson(jsonBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return {};
    }

    QJsonArray markersArray;
    if (jsonDocument.isArray()) {
        markersArray = jsonDocument.array();
    } else if (jsonDocument.isObject()) {
        const QJsonObject rootObject = jsonDocument.object();
        const QJsonValue notesValue = rootObject.value(QStringLiteral("notes"));
        if (notesValue.isArray()) {
            markersArray = notesValue.toArray();
        }
    }

    QVector<UserMarker> parsedMarkers;
    parsedMarkers.reserve(markersArray.size());
    for (const QJsonValue &markerValue : markersArray) {
        if (!markerValue.isObject()) {
            continue;
        }

        const QJsonObject markerObject = markerValue.toObject();
        bool frameOk = false;
        qint32 frame = -1;
        if (markerObject.contains(QStringLiteral("frame"))) {
            frame = jsonValueToInt(markerObject.value(QStringLiteral("frame")), &frameOk);
        } else if (markerObject.contains(QStringLiteral("selection"))) {
            frame = jsonValueToInt(markerObject.value(QStringLiteral("selection")), &frameOk);
        }
        if (!frameOk || frame <= 0) {
            continue;
        }

        QString comment;
        const QJsonValue commentValue = markerObject.value(QStringLiteral("comment"));
        if (commentValue.isString()) {
            comment = commentValue.toString();
        } else {
            const QJsonValue textValue = markerObject.value(QStringLiteral("text"));
            if (textValue.isString()) {
                comment = textValue.toString();
            }
        }

        parsedMarkers.append({frame, comment});
    }

    return normaliseUserMarkers(parsedMarkers, totalFrames);
}

QVector<UserMarker> userMarkersFromVideoParameters(const LdDecodeMetaData::VideoParameters &videoParameters,
                                                   qint32 totalFrames)
{
    QVector<UserMarker> combinedMarkers;
    if (videoParameters.userMarkerSelection > 0) {
        combinedMarkers.append({videoParameters.userMarkerSelection, videoParameters.userMarkerComment});
    }
    combinedMarkers += parseUserMarkersJson(videoParameters.userMarkersJson, totalFrames);
    return normaliseUserMarkers(combinedMarkers, totalFrames);
}

QString frameToSimpleTimecode(qint32 frame, VideoSystem system)
{
    if (frame <= 0) {
        return QStringLiteral("00:00:00:00");
    }

    const qint32 fps = system == PAL ? 25 : 30;
    const qint32 zeroBasedFrame = frame - 1;
    const qint32 framesPerHour = fps * 60 * 60;
    const qint32 framesPerMinute = fps * 60;

    const qint32 hours = zeroBasedFrame / framesPerHour;
    const qint32 minutes = (zeroBasedFrame % framesPerHour) / framesPerMinute;
    const qint32 seconds = (zeroBasedFrame % framesPerMinute) / fps;
    const qint32 frameInSecond = zeroBasedFrame % fps;

    return QStringLiteral("%1:%2:%3:%4")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(frameInSecond, 2, 10, QLatin1Char('0'));
}

QString sanitizeMarkerComment(const QString &comment)
{
    QString sanitized = comment;
    sanitized.replace(QRegularExpression(QStringLiteral("[\\r\\n\\t]+")), QStringLiteral(" "));
    return sanitized.trimmed();
}

QString escapedCsvField(const QString &unescapedString)
{
    if (!unescapedString.contains(QLatin1Char(','))
        && !unescapedString.contains(QLatin1Char('\"'))
        && !unescapedString.contains(QLatin1Char('\n'))
        && !unescapedString.contains(QLatin1Char('\r'))) {
        return unescapedString;
    }

    QString escaped = unescapedString;
    escaped.replace(QLatin1Char('\"'), QStringLiteral("\"\""));
    return '\"' + escaped + '\"';
}

bool writeVitsCsv(LdDecodeMetaData &metaData, const QString &fileName)
{
    // Open a file for the CSV output
    QFile csvFile(fileName);
    if (!csvFile.open(QFile::WriteOnly | QFile::Text)) {
        tbcDebug(QStringLiteral("LdDecodeMetaData::writeVitsCsv(): Could not open CSV file for output!"));
        return false;
    }

    // Create a text stream for the CSV output
    QTextStream outStream(&csvFile);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    outStream.setCodec("UTF-8");
#endif

    // Write the field and VITS data
    outStream << "seqNo,isFirstField,syncConf,";
    outStream << "medianBurstIRE,fieldPhaseID,audioSamples,";

    // VITS headers
    outStream << "wSNR,bPSNR";
    outStream << '\n';

    for (qint32 fieldNumber = 1; fieldNumber <= metaData.getNumberOfFields(); fieldNumber++) {
        const LdDecodeMetaData::Field &field = metaData.getField(fieldNumber);
        outStream << escapedString(QString::number(field.seqNo)) << ",";
        outStream << escapedString(QString::number(field.isFirstField)) << ",";
        outStream << escapedString(QString::number(field.syncConf)) << ",";
        outStream << escapedString(QString::number(field.medianBurstIRE)) << ",";
        outStream << escapedString(QString::number(field.fieldPhaseID)) << ",";
        outStream << escapedString(QString::number(field.audioSamples)) << ",";

        outStream << escapedString(QString::number(field.vitsMetrics.wSNR)) << ",";
        outStream << escapedString(QString::number(field.vitsMetrics.bPSNR)) << ",";

        outStream << '\n';
    }

    // Close the CSV file
    csvFile.close();

    return true;
}

bool writeUserMarkersCsv(LdDecodeMetaData &metaData, const QString &fileName)
{
    QFile csvFile(fileName);
    if (!csvFile.open(QFile::WriteOnly | QFile::Text)) {
        tbcDebug(QStringLiteral("writeUserMarkersCsv: Could not open CSV file for output"));
        return false;
    }

    QTextStream outStream(&csvFile);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    outStream.setCodec("UTF-8");
#endif

    const LdDecodeMetaData::VideoParameters videoParameters = metaData.getVideoParameters();
    const QVector<UserMarker> markers = userMarkersFromVideoParameters(videoParameters,
                                                                       metaData.getNumberOfFrames());

    outStream << "frame,timecode,comment\n";
    for (const UserMarker &marker : markers) {
        if (marker.frame <= 0) {
            continue;
        }
        const QString timecode = frameToSimpleTimecode(marker.frame, videoParameters.system);
        const QString markerComment = sanitizeMarkerComment(marker.comment);
        outStream << escapedCsvField(QString::number(marker.frame)) << ",";
        outStream << escapedCsvField(timecode) << ",";
        outStream << escapedCsvField(markerComment) << "\n";
    }

    csvFile.close();
    return true;
}

bool writeUserMarkersTxt(LdDecodeMetaData &metaData, const QString &fileName)
{
    QFile textFile(fileName);
    if (!textFile.open(QFile::WriteOnly | QFile::Text)) {
        tbcDebug(QStringLiteral("writeUserMarkersTxt: Could not open text file for output"));
        return false;
    }

    QTextStream outStream(&textFile);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    outStream.setCodec("UTF-8");
#endif

    const LdDecodeMetaData::VideoParameters videoParameters = metaData.getVideoParameters();
    const QVector<UserMarker> markers = userMarkersFromVideoParameters(videoParameters,
                                                                       metaData.getNumberOfFrames());

    outStream << "User Markers Log\n";
    outStream << "frame\ttimecode\tcomment\n";
    for (const UserMarker &marker : markers) {
        if (marker.frame <= 0) {
            continue;
        }
        const QString timecode = frameToSimpleTimecode(marker.frame, videoParameters.system);
        const QString markerComment = sanitizeMarkerComment(marker.comment);
        outStream << marker.frame << "\t" << timecode << "\t" << markerComment << "\n";
    }

    textFile.close();
    return true;
}

bool writeVbiCsv(LdDecodeMetaData &metaData, const QString &fileName)
{
    // Open a file for the CSV output
    QFile csvFile(fileName);
    if (!csvFile.open(QFile::WriteOnly | QFile::Text)) {
        tbcDebug(QStringLiteral("LdDecodeMetaData::writeVbiCsv(): Could not open CSV file for output!"));
        return false;
    }

    // Create a text stream for the CSV output
    QTextStream outStream(&csvFile);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    outStream.setCodec("UTF-8");
#endif

    // Write the field and VBI data
    outStream << "frameNo,";
    outStream << "discType,pictureNumber,clvTimeCode,chapter,";
    outStream << "leadIn,leadOut,userCode,stopCode";
    outStream << '\n';

    for (qint32 frameNumber = 1; frameNumber <= metaData.getNumberOfFrames(); frameNumber++) {
        // Get the required field numbers
        qint32 firstFieldNumber = metaData.getFirstFieldNumber(frameNumber);
        qint32 secondFieldNumber = metaData.getSecondFieldNumber(frameNumber);

        // Get the field metadata
        const LdDecodeMetaData::Field &firstField = metaData.getField(firstFieldNumber);
        const LdDecodeMetaData::Field &secondField = metaData.getField(secondFieldNumber);

        qint32 vbi16_1, vbi17_1, vbi18_1;
        qint32 vbi16_2, vbi17_2, vbi18_2;

        vbi16_1 = firstField.vbi.vbiData[0];
        vbi17_1 = firstField.vbi.vbiData[1];
        vbi18_1 = firstField.vbi.vbiData[2];
        vbi16_2 = secondField.vbi.vbiData[0];
        vbi17_2 = secondField.vbi.vbiData[1];
        vbi18_2 = secondField.vbi.vbiData[2];

        VbiDecoder vbiDecoder;
        VbiDecoder::Vbi vbi = vbiDecoder.decodeFrame(vbi16_1, vbi17_1, vbi18_1, vbi16_2, vbi17_2, vbi18_2);

        outStream << escapedString(QString::number(frameNumber)) << ",";

        if (vbi.type != VbiDecoder::VbiDiscTypes::unknownDiscType) {
            if (vbi.type == VbiDecoder::VbiDiscTypes::cav) outStream << "CAV,";
            if (vbi.type == VbiDecoder::VbiDiscTypes::clv) outStream << "CLV,";
        } else {
            if (vbi.type == VbiDecoder::VbiDiscTypes::cav) outStream << "CAV,";
            if (vbi.type == VbiDecoder::VbiDiscTypes::clv) outStream << "CLV,";
            if (vbi.type == VbiDecoder::VbiDiscTypes::unknownDiscType) outStream << "unknown,";
        }

        if (vbi.picNo != -1) outStream << escapedString(QString::number(vbi.picNo)) << ",";
        else outStream << "none,";

        QString clvTimecodeString;
        if (vbi.clvHr != -1 || vbi.clvMin != -1 || vbi.clvSec != -1 || vbi.clvPicNo != -1) {
            if (vbi.clvHr != -1 && vbi.clvMin != -1) {
                clvTimecodeString = QString("%1").arg(vbi.clvHr, 2, 10, QChar('0')) + ":" + QString("%1").arg(vbi.clvMin, 2, 10, QChar('0')) + ":";
            } else clvTimecodeString = "xx:xx:";

            if (vbi.clvSec != -1 && vbi.clvPicNo != -1) {
                clvTimecodeString += QString("%1").arg(vbi.clvSec, 2, 10, QChar('0')) + "." + QString("%1").arg(vbi.clvPicNo, 2, 10, QChar('0'));
            } else clvTimecodeString += "xx.xx";
        } else if (vbi.clvHr != -1 || vbi.clvMin != -1) {
            if (vbi.clvHr != -1 && vbi.clvMin != -1) {
                clvTimecodeString = QString("%1").arg(vbi.clvHr, 2, 10, QChar('0')) + ":" + QString("%1").arg(vbi.clvMin, 2, 10, QChar('0'));
            } else clvTimecodeString = "xx:xx";
        } else clvTimecodeString = "none";
        outStream << escapedString(clvTimecodeString) << ",";

        if (vbi.chNo != -1) outStream << escapedString(QString::number(vbi.chNo)) << ",";
        else outStream << "none,";

        if (vbi.leadIn) outStream << "true,";
        else outStream << "false,";

        if (vbi.leadOut) outStream << "true,";
        else outStream << "false,";

        if (vbi.userCode.isEmpty()) outStream << "none,";
        else {
            if (vbi.userCode.isEmpty()) outStream << "0,";
            else outStream << escapedString(vbi.userCode) << ",";
        }

        if (vbi.picStop) outStream << "true,";
        else outStream << "false,";

        outStream << '\n';
    }

    // Close the CSV file
    csvFile.close();

    return true;
}
