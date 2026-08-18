/******************************************************************************
 * cudapluginmanager.h
 * ld-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Harry Munday
 *
 * This file is part of tbc-tools.
 *
 * CudaPluginManager handles the opt-in CUDA runtime plugin lifecycle:
 * querying the latest release on harrypm/tbc-tools-ci-cache via the GitHub
 * Releases API, parsing the plugin manifest, downloading the platform package,
 * SHA-256-verifying every file against the manifest, and installing/removing
 * the plugin under <appDir>/plugins/cuda. Inspired by decode-orc's plugin
 * architecture (manifest + SHA-256 verification + trust state), adapted to
 * tbc-tools' single-plugin, ORT-CUDA-EP-loaded-at-runtime model.
 ******************************************************************************/

#ifndef CUDAPLUGINMANAGER_H
#define CUDAPLUGINMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QNetworkAccessManager>

class QNetworkReply;

class CudaPluginManager : public QObject
{
    Q_OBJECT

public:
    explicit CudaPluginManager(QObject *parent = nullptr);
    ~CudaPluginManager() override;

    // The CI/cache repo that hosts the CUDA plugin release assets.
    static const QString cacheRepositoryOwner;
    static const QString cacheRepositoryName;

    // The plugin id (used to match the manifest asset + for display).
    static const QString pluginId;

    // Default install directory (app-relative): <appDir>/plugins/cuda
    static QString defaultInstallDirectory();

    // Current platform string ("linux" or "windows") and arch ("x86_64").
    static QString currentPlatform();
    static QString currentArch();

    // True if the plugin appears installed (manifest + at least one file present
    // at the configured install path). Does not verify SHA-256.
    bool isInstalled() const;

    // Asynchronously query the latest release on the cache repo. Emits
    // latestReleaseResolved or releaseCheckFailed.
    void checkForUpdate();

    // Asynchronously download the platform package from the resolved release,
    // verify each file's SHA-256 against the manifest, and install to
    // installDirectory(). Emits installProgress, installSucceeded, or
    // installFailed. Requires a prior successful checkForUpdate().
    void downloadAndInstall(const QString &installDirectory = QString());

    // Remove the installed plugin (deletes the install directory tree).
    // Emits removeSucceeded or removeFailed.
    void remove();

    // Resolved release metadata (valid after latestReleaseResolved).
    QString latestVersion() const { return m_latestVersion; }
    QString latestReleaseTag() const { return m_latestReleaseTag; }
    QString latestReleaseUrl() const { return m_latestReleaseUrl; }

private slots:
    void handleReleaseReply(QNetworkReply *reply);
    void handleAssetDownloadReply(QNetworkReply *reply);

signals:
    // checkForUpdate results
    void latestReleaseResolved(const QString &version, const QString &releaseTag, const QString &releaseUrl);
    void releaseCheckFailed(const QString &errorString);

    // downloadAndInstall progress + results
    void installProgress(qint64 bytesReceived, qint64 bytesTotal, const QString &currentFile);
    void installSucceeded(const QString &installPath);
    void installFailed(const QString &errorString);

    // remove results
    void removeSucceeded();
    void removeFailed(const QString &errorString);

private:
    QNetworkAccessManager *m_networkManager;
    QString m_latestVersion;
    QString m_latestReleaseTag;
    QString m_latestReleaseUrl;
    QString m_manifestAssetUrl;      // browser_download_url of the manifest JSON
    QString m_packageAssetUrl;       // browser_download_url of the platform package (zip/tar.gz)
    QString m_packageAssetName;
    QString m_manifestAssetName;
    QString m_targetInstallDir;

    // Pending download state for multi-file install progress.
    int m_pendingFileCount = 0;
    int m_completedFileCount = 0;
    qint64 m_totalBytes = 0;
    qint64 m_receivedBytes = 0;

    bool m_requestInFlight = false;

    // Helper: find the manifest + package asset URLs for the current platform
    // from a GitHub release JSON payload. Returns true if both found.
    bool resolveAssetsFromReleaseJson(const QByteArray &payload, QString &error);
};

#endif // CUDAPLUGINMANAGER_H
