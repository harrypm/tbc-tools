/************************************************************************

    testsourcefield.cpp

    Unit tests for SourceField loading behavior
    Copyright (C) 2026

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

#include <QDataStream>
#include <QDebug>
#include <QFile>
#include <QTemporaryDir>

#include "sourcefield.h"

namespace {
bool writeTestTbc(const QString &filePath, qint32 fieldLength)
{
    QFile outputFile(filePath);
    if (!outputFile.open(QIODevice::WriteOnly)) {
        qCritical() << "Failed to open test TBC for writing:" << filePath;
        return false;
    }

    QDataStream stream(&outputFile);
    stream.setByteOrder(QDataStream::LittleEndian);

    // Write exactly two fields.
    for (qint32 i = 0; i < fieldLength; i++) {
        stream << static_cast<quint16>(1000 + i);
    }
    for (qint32 i = 0; i < fieldLength; i++) {
        stream << static_cast<quint16>(2000 + i);
    }

    return true;
}

void initializeMetadataWithMoreFieldsThanSource(TbcMetaData &metadata,
                                                qint32 metadataFields,
                                                qint32 fieldWidth,
                                                qint32 fieldHeight,
                                                qint32 blackLevel)
{

    TbcMetaData::VideoParameters videoParameters;
    videoParameters.system = NTSC;
    videoParameters.fieldWidth = fieldWidth;
    videoParameters.fieldHeight = fieldHeight;
    videoParameters.black16bIre = blackLevel;
    videoParameters.white16bIre = 0xFFFF;
    videoParameters.blanking16bIre = blackLevel;
    videoParameters.numberOfSequentialFields = 0;
    videoParameters.isValid = true;
    metadata.setVideoParameters(videoParameters);

    for (qint32 i = 0; i < metadataFields; i++) {
        TbcMetaData::Field field;
        field.isFirstField = (i % 2) == 0;
        field.audioSamples = 0;
        metadata.appendField(field);
    }
}
} // namespace

int main()
{
    const qint32 fieldWidth = 4;
    const qint32 fieldHeight = 2;
    const qint32 fieldLength = fieldWidth * fieldHeight;
    const qint32 blackLevel = 0x2222;

    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid()) {
        qCritical() << "Failed to create temporary directory for test data";
        return 1;
    }
    const QString tbcPath = temporaryDirectory.path() + "/input.tbc";
    if (!writeTestTbc(tbcPath, fieldLength)) {
        return 1;
    }

    // Source has 2 fields (1 frame), metadata claims 4 fields (2 frames).
    TbcMetaData metadata;
    initializeMetadataWithMoreFieldsThanSource(metadata, 4, fieldWidth, fieldHeight, blackLevel);

    SourceVideo sourceVideo;
    if (!sourceVideo.open(tbcPath, fieldLength)) {
        qCritical() << "Failed to open test source video";
        return 1;
    }
    if (sourceVideo.getNumberOfAvailableFields() != 2) {
        qCritical() << "Unexpected source field count:" << sourceVideo.getNumberOfAvailableFields();
        return 1;
    }

    QVector<SourceField> loadedFields;
    qint32 startIndex = -1;
    qint32 endIndex = -1;

    // Frame 1 should load real data from disk.
    SourceField::loadFields(sourceVideo, metadata, 1, 1, 0, 0, loadedFields, startIndex, endIndex);
    if (startIndex != 0 || endIndex != 2 || loadedFields.size() != 2) {
        qCritical() << "Unexpected frame-1 index sizing from SourceField::loadFields";
        return 1;
    }
    if (loadedFields[0].data.size() != fieldLength || loadedFields[1].data.size() != fieldLength) {
        qCritical() << "Unexpected frame-1 data length";
        return 1;
    }
    if (loadedFields[0].data[0] != 1000 || loadedFields[1].data[0] != 2000) {
        qCritical() << "Frame-1 source data did not match expected on-disk values";
        return 1;
    }

    // Frame 2 points past the end of the source file and must not crash.
    // It should return blank (black-level) fields instead.
    SourceField::loadFields(sourceVideo, metadata, 2, 1, 0, 0, loadedFields, startIndex, endIndex);
    if (startIndex != 0 || endIndex != 2 || loadedFields.size() != 2) {
        qCritical() << "Unexpected frame-2 index sizing from SourceField::loadFields";
        return 1;
    }
    if (loadedFields[0].data.size() != fieldLength || loadedFields[1].data.size() != fieldLength) {
        qCritical() << "Unexpected frame-2 data length";
        return 1;
    }
    for (qint32 i = 0; i < fieldLength; i++) {
        if (loadedFields[0].data[i] != blackLevel || loadedFields[1].data[i] != blackLevel) {
            qCritical() << "Frame-2 did not fall back to black data at sample" << i;
            return 1;
        }
    }

    return 0;
}
