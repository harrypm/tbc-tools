#ifndef METADATACONVERTERUTIL_H
#define METADATACONVERTERUTIL_H

#include <QString>

namespace MetadataConverterUtil {
QString resolveMetadataConverterPath();
QString defaultMetadataOutputPath(const QString &inputFilename, bool jsonToSqlite);
bool runMetadataConverter(const QString &direction,
                          const QString &inputFilename,
                          const QString &outputFilename,
                          QString *errorMessage = nullptr);
}

#endif // METADATACONVERTERUTIL_H
