/******************************************************************************
 * plugincatalog.cpp
 * ld-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Harry Munday
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#include "plugincatalog.h"
#include "tbc/logging.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QSet>
#include <QUrl>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>

const QString PluginCatalog::repositoryOwner = QStringLiteral("harrypm");
const QString PluginCatalog::repositoryName = QStringLiteral("tbc-tools");

PluginCatalog::PluginCatalog(QObject *parent)
    : QObject(parent),
      m_networkManager(new QNetworkAccessManager(this))
{
    connect(m_networkManager, &QNetworkAccessManager::finished,
            this, &PluginCatalog::handleReply);
}

PluginCatalog::~PluginCatalog() = default;

QString PluginCatalog::pluginsRootDirectory()
{
    // Same OS-appropriate, update-persistent convention as the CUDA plugin
    // install dir (see cudapluginmanager.cpp:defaultInstallDirectory). Generic
    // plugins install under <root>/<id>; the catalog cache lives at
    // <root>/catalog.cache.json.
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (!base.isEmpty()) {
        return QDir(base).filePath(QStringLiteral("tbc-tools/plugins"));
    }
    return QDir::homePath() + QStringLiteral("/.tbc-tools/plugins");
}

QString PluginCatalog::cachedCatalogPath()
{
    return QDir(pluginsRootDirectory()).filePath(QStringLiteral("catalog.cache.json"));
}

bool PluginCatalog::loadBundled()
{
    QFile file(QStringLiteral(":/plugins/catalog.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        tbcDebugStream() << "PluginCatalog::loadBundled(): could not open bundled catalog resource";
        return false;
    }
    const QByteArray payload = file.readAll();
    file.close();
    QString error;
    if (!parseCatalog(payload, error)) {
        tbcDebugStream() << "PluginCatalog::loadBundled(): invalid bundled catalog:" << error;
        return false;
    }
    return true;
}

bool PluginCatalog::loadCached()
{
    const QString path = cachedCatalogPath();
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray payload = file.readAll();
    file.close();
    QString error;
    if (!parseCatalog(payload, error)) {
        tbcDebugStream() << "PluginCatalog::loadCached(): invalid cached catalog:" << error;
        return false;
    }
    return true;
}

void PluginCatalog::fetchRemote()
{
    if (m_requestInFlight) {
        tbcDebugStream() << "PluginCatalog::fetchRemote(): request in flight; ignoring";
        return;
    }
    // CDN-first, raw-fallback to insulate against raw.githubusercontent.com
    // rate limiting (HTTP 429) -- same pattern as the arm64 gas-preprocessor
    // pre-fetch (see AGENTS.md).
    m_fetchUrls.clear();
    m_fetchUrls << QStringLiteral("https://cdn.jsdelivr.net/gh/%1/%2@main/plugins/catalog.json")
                      .arg(repositoryOwner, repositoryName);
    m_fetchUrls << QStringLiteral("https://raw.githubusercontent.com/%1/%2/main/plugins/catalog.json")
                      .arg(repositoryOwner, repositoryName);
    m_fetchAttempt = 0;

    QNetworkRequest request{QUrl(m_fetchUrls.at(m_fetchAttempt))};
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("tbc-tools/%1 (ld-analyse plugin catalog)")
                          .arg(QString::fromUtf8(APP_VERSION)));
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    tbcDebugStream() << "PluginCatalog::fetchRemote(): GET" << m_fetchUrls.at(m_fetchAttempt);
    m_requestInFlight = true;
    m_networkManager->get(request);
}

void PluginCatalog::handleReply(QNetworkReply *reply)
{
    reply->deleteLater();

    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool transportOk = (reply->error() == QNetworkReply::NoError);
    const QByteArray payload = transportOk ? reply->readAll() : QByteArray();

    // If this source failed at the transport or HTTP level, try the next URL
    // before giving up. (A 200 with invalid JSON is not retried here; that
    // would only happen if a source served garbage, which is unlikely.)
    if (!transportOk || statusCode != 200) {
        tbcDebugStream() << "PluginCatalog::handleReply(): source" << m_fetchAttempt
                         << "failed (error=" << reply->error() << "http=" << statusCode << ")";
        m_fetchAttempt++;
        if (m_fetchAttempt < m_fetchUrls.size()) {
            QNetworkRequest request{QUrl(m_fetchUrls.at(m_fetchAttempt))};
            request.setHeader(QNetworkRequest::UserAgentHeader,
                              QStringLiteral("tbc-tools/%1 (ld-analyse plugin catalog)")
                                  .arg(QString::fromUtf8(APP_VERSION)));
            request.setRawHeader("Accept", "application/json");
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                 QNetworkRequest::NoLessSafeRedirectPolicy);
            tbcDebugStream() << "PluginCatalog::handleReply(): falling back to GET"
                             << m_fetchUrls.at(m_fetchAttempt);
            // Still in flight; m_requestInFlight left true.
            m_networkManager->get(request);
            return;
        }
        m_requestInFlight = false;
        emit fetchFailed(tr("Could not download the plugin catalog (last HTTP status %1).").arg(statusCode));
        return;
    }

    m_requestInFlight = false;
    QString error;
    if (!parseCatalog(payload, error)) {
        emit fetchFailed(tr("Plugin catalog was downloaded but is invalid: %1").arg(error));
        return;
    }
    saveCached(payload);
    tbcDebugStream() << "PluginCatalog::handleReply(): fetched catalog with"
                     << m_entries.size() << "plugin(s)";
    emit catalogFetched(m_entries);
}

bool PluginCatalog::parseCatalog(const QByteArray &payload, QString &error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        error = tr("JSON parse error: %1").arg(parseError.errorString());
        return false;
    }
    const QJsonObject obj = doc.object();
    const int version = obj.value(QStringLiteral("catalog_version")).toInt(-1);
    if (version != 1) {
        error = tr("Unsupported catalog_version: %1 (expected 1).").arg(version);
        return false;
    }
    const QJsonArray plugins = obj.value(QStringLiteral("plugins")).toArray();
    QList<PluginCatalogEntry> entries;
    QSet<QString> seenIds;
    for (const QJsonValue &v : plugins) {
        const QJsonObject p = v.toObject();
        PluginCatalogEntry e;
        e.id = p.value(QStringLiteral("id")).toString().trimmed();
        e.displayName = p.value(QStringLiteral("display_name")).toString().trimmed();
        e.description = p.value(QStringLiteral("description")).toString().trimmed();
        e.category = p.value(QStringLiteral("category")).toString().trimmed();
        e.backend = p.value(QStringLiteral("backend")).toString().trimmed().toLower();
        e.homepage = p.value(QStringLiteral("homepage")).toString().trimmed();
        if (e.id.isEmpty() || e.displayName.isEmpty() || e.description.isEmpty()
            || e.category.isEmpty() || e.backend.isEmpty()) {
            tbcDebugStream() << "PluginCatalog: skipping entry with missing required fields (id=" << e.id << ")";
            continue;
        }
        if (e.backend != QStringLiteral("cuda-runtime") && e.backend != QStringLiteral("generic")) {
            tbcDebugStream() << "PluginCatalog: skipping entry" << e.id
                             << "with unknown backend" << e.backend;
            continue;
        }
        if (e.backend == QStringLiteral("generic")) {
            e.version = p.value(QStringLiteral("version")).toString().trimmed();
            e.packageUrl = p.value(QStringLiteral("package_url")).toString().trimmed();
            const QJsonArray files = p.value(QStringLiteral("files")).toArray();
            if (e.version.isEmpty() || e.packageUrl.isEmpty() || files.isEmpty()) {
                tbcDebugStream() << "PluginCatalog: skipping generic entry" << e.id
                                 << "with missing version/package_url/files";
                continue;
            }
            for (const QJsonValue &fv : files) {
                const QJsonObject fo = fv.toObject();
                PluginCatalogFile pf;
                pf.name = fo.value(QStringLiteral("name")).toString().trimmed();
                pf.sha256 = fo.value(QStringLiteral("sha256")).toString().trimmed().toLower();
                if (pf.name.isEmpty() || pf.sha256.isEmpty()) {
                    continue;
                }
                e.files.append(pf);
            }
            if (e.files.isEmpty()) {
                tbcDebugStream() << "PluginCatalog: skipping generic entry" << e.id
                                 << "with no valid file entries";
                continue;
            }
        }
        if (seenIds.contains(e.id)) {
            tbcDebugStream() << "PluginCatalog: skipping duplicate plugin id" << e.id;
            continue;
        }
        seenIds.insert(e.id);
        entries.append(e);
    }
    m_entries = entries;
    return true;
}

void PluginCatalog::saveCached(const QByteArray &payload) const
{
    const QString path = cachedCatalogPath();
    const QDir dir = QFileInfo(path).dir();
    if (!dir.mkpath(dir.absolutePath())) {
        tbcDebugStream() << "PluginCatalog::saveCached(): could not create dir" << dir.absolutePath();
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        tbcDebugStream() << "PluginCatalog::saveCached(): could not open" << path << "for write";
        return;
    }
    file.write(payload);
    file.close();
}
