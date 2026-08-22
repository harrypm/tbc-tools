/******************************************************************************
 * genericplugininstaller.h
 * ld-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Harry Munday
 *
 * This file is part of tbc-tools.
 *
 * GenericPluginInstaller is the self-contained backend for catalog entries
 * with backend == "generic". It downloads the entry's package_url, extracts
 * it via tar, SHA-256-verifies every file against the catalog manifest, and
 * writes an install record (plugin.json). It does NOT touch CudaPluginManager
 * or the cudaPlugin Configuration group. See docs/plugins.md.
 ******************************************************************************/

#ifndef GENERICPLUGININSTALLER_H
#define GENERICPLUGININSTALLER_H

#include <QObject>
#include <QString>
#include "plugincatalog.h"

class QNetworkAccessManager;
class QNetworkReply;

// Install record read back from <installDir>/plugin.json.
struct GenericPluginInstalledInfo {
    bool installed = false;
    QString version;
    QString installPath;
};

class GenericPluginInstaller : public QObject
{
    Q_OBJECT

public:
    explicit GenericPluginInstaller(QObject *parent = nullptr);
    ~GenericPluginInstaller() override;

    // <pluginsRootDirectory()>/<id>
    static QString installDirectoryFor(const QString &id);

    // Read the install record for a plugin id (installed=false if absent or
    // unreadable). Reads disk only; no network.
    GenericPluginInstalledInfo installedInfo(const QString &id) const;

    // Download entry.packageUrl, extract via tar, SHA-256 verify entry.files,
    // write <installDir>/plugin.json, emit installSucceeded or installFailed.
    void install(const PluginCatalogEntry &entry);

    // Delete the install directory for id; emit removeSucceeded/Failed.
    void remove(const QString &id);

    // Abort an in-flight install() download. The outstanding reply's finished
    // handler emits installFailed with an "Operation canceled" error.
    void cancelInstall();

signals:
    void installProgress(qint64 bytesReceived, qint64 bytesTotal, const QString &currentFile);
    void installSucceeded(const QString &installPath);
    void installFailed(const QString &errorString);
    void removeSucceeded();
    void removeFailed(const QString &errorString);

private:
    QNetworkAccessManager *m_networkManager;
    QNetworkReply *m_inFlightReply = nullptr;  // set during install(); used by cancelInstall()
};

#endif // GENERICPLUGININSTALLER_H
