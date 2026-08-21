/******************************************************************************
 * cudapluginmanager.cpp
 * ld-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Harry Munday
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#include "cudapluginmanager.h"
#include "configuration.h"
#include "tbc/logging.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QProcess>
#include <QTemporaryFile>
#include <QStandardPaths>
#include <QRegularExpression>
namespace {
QString archiveStem(const QString &fileName)
{
    QString stem = fileName;
    if (stem.endsWith(QStringLiteral(".tar.gz"), Qt::CaseInsensitive)) {
        stem.chop(7);
        return stem;
    }
    if (stem.endsWith(QStringLiteral(".tgz"), Qt::CaseInsensitive)) {
        stem.chop(4);
        return stem;
    }
    if (stem.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
        stem.chop(4);
        return stem;
    }
    return QFileInfo(fileName).completeBaseName();
}

QString inferManifestPathForArchive(const QString &archivePath,
                                    const QString &platform,
                                    const QString &arch)
{
    const QFileInfo archiveInfo(archivePath);
    const QDir archiveDir = archiveInfo.dir();
    const QString archiveStemValue = archiveStem(archiveInfo.fileName());
    const QString packagePrefix = QStringLiteral("tbc-tools-cuda-plugin-");
    if (archiveStemValue.startsWith(packagePrefix, Qt::CaseInsensitive)) {
        const QString suffix = archiveStemValue.mid(packagePrefix.size());
        const QString exactName = QStringLiteral("tbc-cuda-plugin-%1-manifest.json").arg(suffix);
        const QString exactPath = archiveDir.filePath(exactName);
        if (QFileInfo::exists(exactPath)) {
            return exactPath;
        }
    }

    const QString platformArchName =
        QStringLiteral("tbc-cuda-plugin-%1-%2-manifest.json").arg(platform, arch);
    const QString platformArchPath = archiveDir.filePath(platformArchName);
    if (QFileInfo::exists(platformArchPath)) {
        return platformArchPath;
    }

    const QStringList genericMatches = archiveDir.entryList(
        QStringList() << QStringLiteral("tbc-cuda-plugin-*-manifest.json"),
        QDir::Files | QDir::Readable | QDir::NoSymLinks);
    if (genericMatches.size() == 1) {
        return archiveDir.filePath(genericMatches.constFirst());
    }
    return QString();
}
} // namespace

const QString CudaPluginManager::cacheRepositoryOwner = QStringLiteral("harrypm");
const QString CudaPluginManager::cacheRepositoryName = QStringLiteral("tbc-tools-ci-cache");
const QString CudaPluginManager::pluginId = QStringLiteral("tbc-tools.cuda-runtime");

CudaPluginManager::CudaPluginManager(QObject *parent)
    : QObject(parent),
      m_networkManager(new QNetworkAccessManager(this))
{
    // The release-check flow uses the manager's global finished signal. Asset
    // downloads (manifest + package) use per-reply lambdas to avoid routing
    // ambiguity; they set a "cudaPluginPhase" property so this handler skips them.
    connect(m_networkManager, &QNetworkAccessManager::finished,
            this, [this](QNetworkReply *reply) {
        if (reply->property("cudaPluginPhase").isValid()) {
            return; // asset download -- handled by its own lambda
        }
        handleReleaseReply(reply);
    });
}

CudaPluginManager::~CudaPluginManager() = default;

QString CudaPluginManager::defaultInstallDirectory()
{
    // Use a writable, OS-appropriate, update-persistent location so the
    // installed plugin survives an app update (the app dir is read-only for
    // Nix/AppImage/Windows-installed builds). Use GenericDataLocation + a
    // fixed "/tbc-tools/plugins/cuda" suffix (NOT AppDataLocation, which
    // appends the per-binary application name and would put ld-analyse's
    // plugin dir at a different path than ld-chroma-decoder's).
    //   Linux:   ~/.local/share/tbc-tools/plugins/cuda  (XDG_DATA_HOME)
    //   Windows: %LOCALAPPDATA%/tbc-tools/plugins/cuda
    //   macOS:   ~/Library/Application Support/tbc-tools/plugins/cuda
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (!base.isEmpty()) {
        return QDir(base).filePath(QStringLiteral("tbc-tools/plugins/cuda"));
    }
    // Fallback if GenericDataLocation is empty (rare).
    return QDir::homePath() + QStringLiteral("/.tbc-tools/plugins/cuda");
}

QString CudaPluginManager::currentPlatform()
{
#if defined(Q_OS_LINUX)
    return QStringLiteral("linux");
#elif defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return QStringLiteral("unknown");
#endif
}

QString CudaPluginManager::currentArch()
{
#if defined(Q_PROCESSOR_X86_64)
    return QStringLiteral("x86_64");
#elif defined(Q_PROCESSOR_ARM)
    return QStringLiteral("arm64");
#else
    return QStringLiteral("unknown");
#endif
}

bool CudaPluginManager::isInstalled() const
{
    Configuration c;
    const QString version = c.getCudaPluginInstalledVersion();
    if (version.isEmpty()) {
        return false;
    }
    const QString path = c.getCudaPluginInstallPath();
    if (path.isEmpty() || !QDir(path).exists()) {
        return false;
    }
    const QStringList entries = QDir(path).entryList(QDir::Files | QDir::NoSymLinks | QDir::NoDotAndDotDot);
    return !entries.isEmpty();
}

void CudaPluginManager::checkForUpdate()
{
    if (m_requestInFlight) {
        tbcDebugStream() << "CudaPluginManager::checkForUpdate(): request in flight; ignoring";
        return;
    }

    const QString apiUrl = QStringLiteral("https://api.github.com/repos/%1/%2/releases/latest")
        .arg(cacheRepositoryOwner, cacheRepositoryName);

    QNetworkRequest request{QUrl(apiUrl)};
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("tbc-tools/%1 (ld-analyse CUDA plugin manager)")
                          .arg(QString::fromUtf8(APP_VERSION)));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    tbcDebugStream() << "CudaPluginManager::checkForUpdate(): GET" << apiUrl;
    m_requestInFlight = true;
    m_networkManager->get(request);
}

bool CudaPluginManager::resolveAssetsFromReleaseJson(const QByteArray &payload, QString &error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        error = QStringLiteral("Could not parse release JSON: %1").arg(parseError.errorString());
        return false;
    }

    const QJsonObject obj = doc.object();
    m_latestReleaseTag = obj.value(QStringLiteral("tag_name")).toString();
    m_latestReleaseUrl = obj.value(QStringLiteral("html_url")).toString();
    if (m_latestReleaseTag.isEmpty()) {
        error = QStringLiteral("Release JSON did not include a tag_name.");
        return false;
    }

    m_latestVersion = m_latestReleaseTag;
    m_latestVersion.remove(QRegularExpression(QStringLiteral("^cuda-plugin-v?"),
                                               QRegularExpression::CaseInsensitiveOption));

    const QString platform = currentPlatform();
    const QString arch = currentArch();
    const QString manifestSuffix = QStringLiteral("-manifest.json");
    const QString manifestPrefix = QStringLiteral("tbc-cuda-plugin-%1-%2").arg(platform, arch);
    const QString packagePrefix = QStringLiteral("tbc-tools-cuda-plugin-%1-%2").arg(platform, arch);

    m_manifestAssetUrl.clear();
    m_manifestAssetName.clear();
    m_packageAssetUrl.clear();
    m_packageAssetName.clear();

    const QJsonArray assets = obj.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue &assetVal : assets) {
        const QJsonObject asset = assetVal.toObject();
        const QString name = asset.value(QStringLiteral("name")).toString();
        const QString url = asset.value(QStringLiteral("browser_download_url")).toString();
        if (name.isEmpty() || url.isEmpty()) {
            continue;
        }
        if (name.startsWith(manifestPrefix)
            && name.endsWith(manifestSuffix)
            && m_manifestAssetUrl.isEmpty()) {
            m_manifestAssetUrl = url;
            m_manifestAssetName = name;
        }
        if (name.startsWith(packagePrefix)
            && (name.endsWith(QStringLiteral(".zip"))
                || name.endsWith(QStringLiteral(".tar.gz")))
            && m_packageAssetUrl.isEmpty()) {
            m_packageAssetUrl = url;
            m_packageAssetName = name;
        }
    }

    if (m_manifestAssetUrl.isEmpty() || m_packageAssetUrl.isEmpty()) {
        error = QStringLiteral("Release %1 does not include a CUDA plugin manifest + "
                               "package for %2-%3 (expected manifest prefix %4, "
                               "package prefix %5).")
                    .arg(m_latestReleaseTag, platform, arch, manifestPrefix, packagePrefix);
        return false;
    }
    return true;
}

void CudaPluginManager::handleReleaseReply(QNetworkReply *reply)
{
    m_requestInFlight = false;
    reply->deleteLater();

    const QVariant statusCodeAttr = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const int statusCode = statusCodeAttr.isValid() ? statusCodeAttr.toInt() : 0;
    if (reply->error() != QNetworkReply::NoError) {
        const QString transportError = reply->errorString().trimmed();
        if (statusCode > 0) {
            emit releaseCheckFailed(
                tr("Network error (HTTP %1): %2").arg(statusCode).arg(transportError));
        } else {
            emit releaseCheckFailed(tr("Network error: %1").arg(transportError));
        }
        return;
    }

    // Check HTTP status after transport succeeded so we can distinguish
    // server responses from network/TLS failures. The /releases/latest
    // endpoint returns 404 when the repo has no published releases yet.
    if (statusCode == 404) {
        emit releaseCheckFailed(
            tr("No CUDA plugin release has been published yet on %1/%2. "
               "A maintainer needs to run the \"Publish CUDA plugin\" workflow "
               "to publish a cuda-plugin-vX release before the plugin can be installed.")
                .arg(cacheRepositoryOwner, cacheRepositoryName));
        return;
    }
    if (statusCode == 403) {
        // GitHub's unauthenticated API rate limit (60 req/hr) or an abuse
        // detection trigger. The reply body usually contains the reason.
        const QByteArray body = reply->readAll();
        QString detail;
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            detail = doc.object().value(QStringLiteral("message")).toString();
        }
        emit releaseCheckFailed(
            tr("GitHub API rate limit reached (HTTP 403). Try again later.%1")
                .arg(detail.isEmpty() ? QString() : QStringLiteral("\n%1").arg(detail)));
        return;
    }
    if (statusCode != 200) {
        emit releaseCheckFailed(tr("GitHub returned HTTP status %1.").arg(statusCode));
        return;
    }

    QString error;
    if (!resolveAssetsFromReleaseJson(reply->readAll(), error)) {
        emit releaseCheckFailed(error);
        return;
    }

    tbcDebugStream() << "CudaPluginManager: resolved release" << m_latestReleaseTag
                     << "manifest=" << m_manifestAssetName
                     << "package=" << m_packageAssetName;

    emit latestReleaseResolved(m_latestVersion, m_latestReleaseTag, m_latestReleaseUrl);
}

void CudaPluginManager::downloadAndInstall(const QString &installDirectory)
{
    if (m_manifestAssetUrl.isEmpty() || m_packageAssetUrl.isEmpty()) {
        emit installFailed(tr("No CUDA plugin release has been resolved. Run checkForUpdate() first."));
        return;
    }
    m_targetInstallDir = installDirectory.isEmpty() ? defaultInstallDirectory() : installDirectory;

    auto makeAssetRequest = [](const QString &url) {
        QNetworkRequest request{QUrl(url)};
        request.setHeader(QNetworkRequest::UserAgentHeader,
                          QStringLiteral("tbc-tools/%1 (ld-analyse CUDA plugin manager)")
                              .arg(QString::fromUtf8(APP_VERSION)));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
        return request;
    };

    // Step 1: download + parse the manifest JSON.
    tbcDebugStream() << "CudaPluginManager: downloading manifest" << m_manifestAssetName;
    emit installProgress(0, 0, m_manifestAssetName);
    QNetworkReply *manifestReply = m_networkManager->get(makeAssetRequest(m_manifestAssetUrl));
    manifestReply->setProperty("cudaPluginPhase", QStringLiteral("manifest"));

    connect(manifestReply, &QNetworkReply::finished, this, [this, manifestReply, makeAssetRequest]() {
        manifestReply->deleteLater();
        if (manifestReply->error() != QNetworkReply::NoError) {
            emit installFailed(tr("Failed to download manifest: %1").arg(manifestReply->errorString()));
            return;
        }
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(manifestReply->readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            emit installFailed(tr("Could not parse manifest JSON: %1").arg(parseError.errorString()));
            return;
        }
        const QJsonObject manifest = doc.object();
        const QJsonArray files = manifest.value(QStringLiteral("files")).toArray();
        if (files.isEmpty()) {
            emit installFailed(tr("Manifest does not declare any files."));
            return;
        }

        // Step 2: download the platform package archive.
        tbcDebugStream() << "CudaPluginManager: downloading package" << m_packageAssetName;
        emit installProgress(0, 0, m_packageAssetName);
        QNetworkReply *packageReply = m_networkManager->get(makeAssetRequest(m_packageAssetUrl));
        packageReply->setProperty("cudaPluginPhase", QStringLiteral("package"));

        connect(packageReply, &QNetworkReply::downloadProgress, this,
                [this, packageAssetName = m_packageAssetName](qint64 recv, qint64 total) {
            emit installProgress(recv, total, packageAssetName);
        });

        connect(packageReply, &QNetworkReply::finished, this,
                [this, packageReply, files, manifest]() {
            packageReply->deleteLater();
            if (packageReply->error() != QNetworkReply::NoError) {
                emit installFailed(tr("Failed to download package: %1").arg(packageReply->errorString()));
                return;
            }
            // Save the archive to a temp file.
            QTemporaryFile tempArchive(QDir::tempPath() + QStringLiteral("/tbc-cuda-plugin-XXXXXX"));
            tempArchive.setAutoRemove(true);
            if (!tempArchive.open() || tempArchive.write(packageReply->readAll()) == -1) {
                emit installFailed(tr("Could not save the downloaded package archive."));
                return;
            }
            tempArchive.close();

            // Step 3: extract the archive to the install dir.
            QDir installDir(m_targetInstallDir);
            if (!installDir.mkpath(m_targetInstallDir)) {
                emit installFailed(tr("Could not create install directory:\n%1").arg(m_targetInstallDir));
                return;
            }

            // tar (available on Linux + Windows 10 1803+ as bsdtar; handles
            // .tar.gz on Linux and .zip on Windows).
            QStringList tarArgs;
            tarArgs << QStringLiteral("-xf") << tempArchive.fileName()
                    << QStringLiteral("-C") << m_targetInstallDir;
            QProcess tar;
            tar.start(QStringLiteral("tar"), tarArgs);
            if (!tar.waitForFinished(120000) || tar.exitStatus() != QProcess::NormalExit || tar.exitCode() != 0) {
                const QString stderrOut = QString::fromUtf8(tar.readAllStandardError()).trimmed();
                emit installFailed(tr("Failed to extract the package (tar exit %1): %2")
                                       .arg(tar.exitCode()).arg(stderrOut));
                return;
            }

            // Step 4: verify each manifest file's SHA-256.
            QStringList failedFiles;
            for (const QJsonValue &fileVal : files) {
                const QJsonObject fileObj = fileVal.toObject();
                const QString fileName = fileObj.value(QStringLiteral("name")).toString();
                const QString expectedSha = fileObj.value(QStringLiteral("sha256")).toString().toLower();
                if (fileName.isEmpty() || expectedSha.isEmpty()) {
                    continue;
                }
                const QString filePath = QDir(m_targetInstallDir).filePath(fileName);
                QFile file(filePath);
                if (!file.open(QIODevice::ReadOnly)) {
                    failedFiles << QStringLiteral("%1 (missing)").arg(fileName);
                    continue;
                }
                QCryptographicHash hash(QCryptographicHash::Sha256);
                if (!hash.addData(&file)) {
                    failedFiles << QStringLiteral("%1 (read error)").arg(fileName);
                    continue;
                }
                const QString actualSha = QString::fromLatin1(hash.result().toHex());
                if (actualSha != expectedSha) {
                    failedFiles << QStringLiteral("%1 (sha256 mismatch)").arg(fileName);
                }
            }

            if (!failedFiles.isEmpty()) {
                // Quarantine: move the install dir to .quarantine
                const QString quarantineDir = m_targetInstallDir + QStringLiteral(".quarantine");
                QDir().rename(m_targetInstallDir, quarantineDir);
                emit installFailed(tr("SHA-256 verification failed for:\n%1\n\nThe plugin has been quarantined.")
                                        .arg(failedFiles.join(QStringLiteral("\n"))));
                return;
            }

            // Step 5: persist to Configuration.
            const QString version = manifest.value(QStringLiteral("plugin_version")).toString();
            Configuration c;
            c.setCudaPluginInstalledVersion(version.isEmpty() ? m_latestVersion : version);
            c.setCudaPluginReleaseTag(m_latestReleaseTag);
            c.setCudaPluginEnabled(true);
            c.setCudaPluginTrusted(true);
            c.setCudaPluginInstallPath(m_targetInstallDir);
            c.writeConfiguration();

            tbcDebugStream() << "CudaPluginManager: installed CUDA plugin v"
                             << c.getCudaPluginInstalledVersion() << "to" << m_targetInstallDir;
            emit installSucceeded(m_targetInstallDir);
        });
    });
}
void CudaPluginManager::installFromLocalArchive(const QString &archivePath,
                                                const QString &installDirectory)
{
    const QFileInfo archiveInfo(archivePath);
    if (!archiveInfo.exists() || !archiveInfo.isFile()) {
        emit installFailed(tr("Archive file does not exist:\n%1")
                               .arg(QDir::toNativeSeparators(archivePath)));
        return;
    }
    const QString archiveNameLower = archiveInfo.fileName().toLower();
    const bool supportedArchive = archiveNameLower.endsWith(QStringLiteral(".zip"))
        || archiveNameLower.endsWith(QStringLiteral(".tar.gz"))
        || archiveNameLower.endsWith(QStringLiteral(".tgz"));
    if (!supportedArchive) {
        emit installFailed(tr("Unsupported archive format for:\n%1\n\n"
                              "Supported formats are .zip, .tar.gz, and .tgz.")
                               .arg(QDir::toNativeSeparators(archiveInfo.fileName())));
        return;
    }

    const QString platform = currentPlatform();
    const QString arch = currentArch();
    const QString manifestPath = inferManifestPathForArchive(archiveInfo.absoluteFilePath(), platform, arch);
    if (manifestPath.isEmpty()) {
        const QString expectedName =
            QStringLiteral("tbc-cuda-plugin-%1-%2-manifest.json").arg(platform, arch);
        emit installFailed(
            tr("Could not locate a matching manifest JSON next to the archive.\n\n"
               "Expected this file in the same directory:\n%1")
                .arg(QDir::toNativeSeparators(expectedName)));
        return;
    }

    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        emit installFailed(tr("Could not open manifest JSON:\n%1")
                               .arg(QDir::toNativeSeparators(manifestPath)));
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument manifestDoc = QJsonDocument::fromJson(manifestFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !manifestDoc.isObject()) {
        emit installFailed(tr("Could not parse manifest JSON (%1): %2")
                               .arg(QDir::toNativeSeparators(manifestPath), parseError.errorString()));
        return;
    }
    const QJsonObject manifest = manifestDoc.object();
    const QString manifestPluginId = manifest.value(QStringLiteral("plugin_id")).toString().trimmed();
    if (!manifestPluginId.isEmpty() && manifestPluginId != pluginId) {
        emit installFailed(
            tr("Manifest plugin_id mismatch:\nexpected %1\nactual %2")
                .arg(pluginId, manifestPluginId));
        return;
    }
    const QString manifestPlatform = manifest.value(QStringLiteral("platform")).toString().trimmed().toLower();
    if (!manifestPlatform.isEmpty() && manifestPlatform != platform) {
        emit installFailed(
            tr("Manifest platform mismatch:\nexpected %1\nactual %2")
                .arg(platform, manifestPlatform));
        return;
    }
    const QString manifestArch = manifest.value(QStringLiteral("arch")).toString().trimmed().toLower();
    if (!manifestArch.isEmpty() && manifestArch != arch) {
        emit installFailed(
            tr("Manifest architecture mismatch:\nexpected %1\nactual %2")
                .arg(arch, manifestArch));
        return;
    }
    const QJsonArray files = manifest.value(QStringLiteral("files")).toArray();
    if (files.isEmpty()) {
        emit installFailed(tr("Manifest does not declare any files."));
        return;
    }

    m_targetInstallDir = installDirectory.isEmpty() ? defaultInstallDirectory() : installDirectory;
    QDir installDir(m_targetInstallDir);
    if (!installDir.mkpath(m_targetInstallDir)) {
        emit installFailed(tr("Could not create install directory:\n%1").arg(m_targetInstallDir));
        return;
    }

    emit installProgress(0, 0, archiveInfo.fileName());
    QStringList tarArgs;
    tarArgs << QStringLiteral("-xf") << archiveInfo.absoluteFilePath()
            << QStringLiteral("-C") << m_targetInstallDir;
    QProcess tar;
    tar.start(QStringLiteral("tar"), tarArgs);
    if (!tar.waitForFinished(120000) || tar.exitStatus() != QProcess::NormalExit || tar.exitCode() != 0) {
        const QString stderrOut = QString::fromUtf8(tar.readAllStandardError()).trimmed();
        emit installFailed(tr("Failed to extract the local archive (tar exit %1): %2")
                               .arg(tar.exitCode()).arg(stderrOut));
        return;
    }

    QStringList failedFiles;
    for (const QJsonValue &fileVal : files) {
        const QJsonObject fileObj = fileVal.toObject();
        const QString fileName = fileObj.value(QStringLiteral("name")).toString();
        const QString expectedSha = fileObj.value(QStringLiteral("sha256")).toString().toLower();
        if (fileName.isEmpty() || expectedSha.isEmpty()) {
            continue;
        }
        const QString filePath = QDir(m_targetInstallDir).filePath(fileName);
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            failedFiles << QStringLiteral("%1 (missing)").arg(fileName);
            continue;
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!hash.addData(&file)) {
            failedFiles << QStringLiteral("%1 (read error)").arg(fileName);
            continue;
        }
        const QString actualSha = QString::fromLatin1(hash.result().toHex());
        if (actualSha != expectedSha) {
            failedFiles << QStringLiteral("%1 (sha256 mismatch)").arg(fileName);
        }
    }
    if (!failedFiles.isEmpty()) {
        const QString quarantineDir = m_targetInstallDir + QStringLiteral(".quarantine");
        QDir().rename(m_targetInstallDir, quarantineDir);
        emit installFailed(tr("SHA-256 verification failed for:\n%1\n\nThe plugin has been quarantined.")
                               .arg(failedFiles.join(QStringLiteral("\n"))));
        return;
    }

    const QString version = manifest.value(QStringLiteral("plugin_version")).toString().trimmed();
    QString releaseTag = manifest.value(QStringLiteral("release_tag")).toString().trimmed();
    if (releaseTag.isEmpty()) {
        releaseTag = archiveInfo.fileName();
    }
    Configuration c;
    c.setCudaPluginInstalledVersion(version.isEmpty() ? QStringLiteral("local") : version);
    c.setCudaPluginReleaseTag(releaseTag);
    c.setCudaPluginEnabled(true);
    c.setCudaPluginTrusted(true);
    c.setCudaPluginInstallPath(m_targetInstallDir);
    c.writeConfiguration();

    tbcDebugStream() << "CudaPluginManager: installed local CUDA plugin from"
                     << archiveInfo.absoluteFilePath() << "using manifest" << manifestPath
                     << "to" << m_targetInstallDir;
    emit installSucceeded(m_targetInstallDir);
}

void CudaPluginManager::handleAssetDownloadReply(QNetworkReply *reply)
{
    // Asset replies are handled by per-reply lambdas (see downloadAndInstall).
    // This slot exists for MOC but is not directly wired.
    reply->deleteLater();
}

void CudaPluginManager::remove()
{
    const QString installDir = Configuration().getCudaPluginInstallPath();
    if (installDir.isEmpty()) {
        emit removeFailed(tr("No CUDA plugin is installed."));
        return;
    }
    QDir dir(installDir);
    if (!dir.exists()) {
        Configuration c;
        c.setCudaPluginInstalledVersion(QString());
        c.setCudaPluginReleaseTag(QString());
        c.setCudaPluginSha256(QString());
        c.setCudaPluginEnabled(false);
        c.setCudaPluginTrusted(false);
        c.setCudaPluginInstallPath(QString());
        c.writeConfiguration();
        emit removeSucceeded();
        return;
    }
    if (!dir.removeRecursively()) {
        emit removeFailed(tr("Could not remove the plugin directory:\n%1").arg(installDir));
        return;
    }
    Configuration c;
    c.setCudaPluginInstalledVersion(QString());
    c.setCudaPluginReleaseTag(QString());
    c.setCudaPluginSha256(QString());
    c.setCudaPluginEnabled(false);
    c.setCudaPluginTrusted(false);
    c.setCudaPluginInstallPath(QString());
    c.writeConfiguration();
    emit removeSucceeded();
}
