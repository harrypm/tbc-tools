/******************************************************************************
 * jsonconverter.h
 * tbc-metadata-converter - Metadata converter tool for ld-decode
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 *
 * This file is part of ld-decode-tools.
 ******************************************************************************/

#ifndef JSONCONVERTER_H
#define JSONCONVERTER_H

#include <QString>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include "lddecodemetadata.h"

class JsonConverter
{
public:
    enum class Direction {
        JsonToSqlite,
        SqliteToJson
    };

    JsonConverter(const QString &inputFilename, const QString &outputFilename, Direction direction);
    ~JsonConverter();

    bool process();

private:
    Direction m_direction;
    QString m_inputFilename;
    QString m_outputFilename;
    QSqlDatabase m_database;
    bool processJsonToSqlite();
    bool processSqliteToJson();
    void reportMetadataContents(LdDecodeMetaData &metaData);
    void reportJsonContents(LdDecodeMetaData &metaData);
    void countDropouts(const LdDecodeMetaData &metaData, qint32 &totalDropouts);
    bool createDatabase();
    bool createSchema();
    bool insertData(LdDecodeMetaData &metaData);
};

#endif // JSONCONVERTER_H