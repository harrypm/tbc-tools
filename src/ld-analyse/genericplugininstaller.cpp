/******************************************************************************
 * genericplugininstaller.cpp
 * ld-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Harry Munday
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#include "genericplugininstaller.h"
#include "tbc/logging.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QProcess>
#include <QCryptographicHash>
#include <QDateTime>
#include <QStandardPaths>

GenericPluginInstaller::GenericPluginInstaller(QObject *parent)
    : QObject(parent),
      m_networkManager(new QNetworkAccessManager(this))
{
}

GenericPluginInstaller::~GenericPluginInstaller() = default;

QString GenericPluginInstaller::installDirectoryFor(const QString &id)
{
    return QDir(PluginCatalog::pluginsRootDirectory()).filePath(id);
}

GenericPluginInstalledInfo GenericPluginInstaller::installedInfo(const QString &id) const
{
    GenericPluginInstalledInfo info;
    const QString installDir = installDirectoryFor(id);
    const QString recordPath = QDir(installDir).filePath(QStringLiteral("plugin.json"));
    QFile file(recordPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return info; // installed = false
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        tbcDebugStream() << "GenericPluginInstaller::installedInfo(): unparseable record for" << id;
        return info;
    }
    const QJsonObject obj = doc.object();
    info.installed = true;
    info.version = obj.value(QStringLiteral("version")).toString().trimmed();
    info.installPath = obj.value(QStringLiteral("install_path")).toString().trimmed();
    if (info.installPath.isEmpty()) {
        info.installPath = installDir;
    }
    return info;
}

void GenericPluginInstaller::install(const PluginCatalogEntry &entry)
{
    if (entry.backend != QStringLiteral("generic")) {
        emit installFailed(tr("Plugin %1 is not a generic-backend plugin.").arg(entry.id));
        return;
    }
    if (entry.packageUrl.isEmpty()) {
        emit installFailed(tr("Plugin %1 has no package URL.").arg(entry.id));
        return;
    }
    if (entry.files.isEmpty()) {
        emit installFailed(tr("Plugin %1 has no files to verify.").arg(entry.id));
        return;
    }

    const QString installDir = installDirectoryFor(entry.id);
    const QString packageUrl = entry.packageUrl;
    const QList<PluginCatalogFile> files = entry.files;
    const QString version = entry.version;
    const QString entryId = entry.id;

    QNetworkRequest request{QUrl(packageUrl)};
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("tbc-tools/%1 (ld-analyse generic plugin installer)")
                          .arg(QString::fromUtf8(APP_VERSION)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    const QString archiveName = QFileInfo(QUrl(packageUrl).path()).fileName();
    tbcDebugStream() << "GenericPluginInstaller: downloading" << packageUrl;
    emit installProgress(0, 0, archiveName);
    QNetworkReply *reply = m_networkManager->get(request);
    m_inFlightReply = reply;

    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 recv, qint64 total) {
        emit installProgress(recv, total, QString());
    });

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, installDir, files, version, packageUrl, entryId]() {
        reply->deleteLater();
        m_inFlightReply = nullptr;
        if (reply->error() != QNetworkReply::NoError) {
            emit installFailed(tr("Failed to download package: %1").arg(reply->errorString()));
            return;
        }
        const QByteArray data = reply->readAll();

        // Save the archive to a temp file.
        QTemporaryFile tempArchive(QDir::tempPath() + QStringLiteral("/tbc-generic-plugin-XXXXXX"));
        tempArchive.setAutoRemove(true);
        if (!tempArchive.open() || tempArchive.write(data) == -1) {
            emit installFailed(tr("Could not save the downloaded package archive."));
            return;
        }
        tempArchive.close();

        // Create the install directory.
        QDir dir(installDir);
        if (!dir.mkpath(installDir)) {
            emit installFailed(tr("Could not create install directory:\n%1").arg(installDir));
            return;
        }

        // Extract via tar (handles .zip on Windows and .tar.gz on Linux).
        QStringList tarArgs;
        tarArgs << QStringLiteral("-xf") << tempArchive.fileName()
                << QStringLiteral("-C") << installDir;
        QProcess tar;
        tar.start(QStringLiteral("tar"), tarArgs);
        if (!tar.waitForFinished(120000) || tar.exitStatus() != QProcess::NormalExit || tar.exitCode() != 0) {
            const QString stderrOut = QString::fromUtf8(tar.readAllStandardError()).trimmed();
            emit installFailed(tr("Failed to extract the package (tar exit %1): %2")
                                   .arg(tar.exitCode()).arg(stderrOut));
            return;
        }

        // SHA-256 verify each catalog file against the extracted file.
        QStringList failedFiles;
        for (const PluginCatalogFile &f : files) {
            const QString filePath = QDir(installDir).filePath(f.name);
            QFile file(filePath);
            if (!file.open(QIODevice::ReadOnly)) {
                failedFiles << QStringLiteral("%1 (missing)").arg(f.name);
                continue;
            }
            QCryptographicHash hash(QCryptographicHash::Sha256);
            if (!hash.addData(&file)) {
                failedFiles << QStringLiteral("%1 (read error)").arg(f.name);
                continue;
            }
            const QString actualSha = QString::fromLatin1(hash.result().toHex());
            if (actualSha != f.sha256.toLower()) {
                failedFiles << QStringLiteral("%1 (sha256 mismatch)").arg(f.name);
            }
        }
        if (!failedFiles.isEmpty()) {
            // Quarantine the partial install.
            const QString quarantineDir = installDir + QStringLiteral(".quarantine");
            QDir().rename(installDir, quarantineDir);
            emit installFailed(tr("SHA-256 verification failed for:\n%1\n\nThe plugin has been quarantined.")
                                   .arg(failedFiles.join(QStringLiteral("\n"))));
            return;
        }

        // Write the install record. Note: catalog files[] must not list
        // "plugin.json" -- this record is authoritative and overwrites any
        // same-named extracted file.
        QJsonObject record;
        record.insert(QStringLiteral("plugin_id"), entryId);
        record.insert(QStringLiteral("version"), version);
        record.insert(QStringLiteral("package_url"), packageUrl);
        record.insert(QStringLiteral("install_path"), installDir);
        record.insert(QStringLiteral("installed_at"),
                      QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        QJsonArray filesArr;
        for (const PluginCatalogFile &f : files) {
            QJsonObject fo;
            fo.insert(QStringLiteral("name"), f.name);
            fo.insert(QStringLiteral("sha256"), f.sha256.toLower());
            filesArr.append(fo);
        }
        record.insert(QStringLiteral("files"), filesArr);
        const QJsonDocument doc(record);
        const QString recordPath = QDir(installDir).filePath(QStringLiteral("plugin.json"));
        QFile recordFile(recordPath);
        if (!recordFile.open(QIODevice::WriteOnly | QIODevice::Truncate)
            || recordFile.write(doc.toJson()) == -1) {
            emit installFailed(tr("Installed but could not write install record:\n%1").arg(recordPath));
            return;
        }
        recordFile.close();

        tbcDebugStream() << "GenericPluginInstaller: installed" << entryId << "to" << installDir;
        emit installSucceeded(installDir);
    });
}

void GenericPluginInstaller::remove(const QString &id)
{
    const QString installDir = installDirectoryFor(id);
    QDir dir(installDir);
    if (!dir.exists()) {
        emit removeFailed(tr("No installed plugin found for %1 at:\n%2").arg(id, installDir));
        return;
    }
    if (!dir.removeRecursively()) {
        emit removeFailed(tr("Could not remove the plugin directory:\n%1").arg(installDir));
        return;
    }
    tbcDebugStream() << "GenericPluginInstaller: removed" << id << "from" << installDir;
    emit removeSucceeded();
}

void GenericPluginInstaller::cancelInstall()
{
    if (m_inFlightReply != nullptr) {
        tbcDebugStream() << "GenericPluginInstaller::cancelInstall(): aborting in-flight download";
        m_inFlightReply->abort();
    }
}
