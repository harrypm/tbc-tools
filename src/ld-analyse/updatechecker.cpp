/******************************************************************************
 * updatechecker.cpp
 * ld-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Harry Munday
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#include "updatechecker.h"
#include "tbc/logging.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QVersionNumber>

const QString UpdateChecker::repositoryOwner = QStringLiteral("harrypm");
const QString UpdateChecker::repositoryName = QStringLiteral("tbc-tools");

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent),
      networkManager(new QNetworkAccessManager(this))
{
    connect(networkManager, &QNetworkAccessManager::finished,
            this, &UpdateChecker::handleReply);
}

UpdateChecker::~UpdateChecker() = default;

QString UpdateChecker::releasesUrl()
{
    return QStringLiteral("https://github.com/%1/%2/releases/latest")
        .arg(repositoryOwner, repositoryName);
}

void UpdateChecker::checkForUpdates()
{
    if (requestInFlight) {
        tbcDebugStream() << "UpdateChecker::checkForUpdates(): a request is already in flight; ignoring";
        return;
    }

    const QString apiUrl = QStringLiteral("https://api.github.com/repos/%1/%2/releases/latest")
        .arg(repositoryOwner, repositoryName);

    QNetworkRequest request{QUrl(apiUrl)};
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("tbc-tools/%1 (ld-analyse update checker)").arg(QString::fromUtf8(APP_VERSION)));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    tbcDebugStream() << "UpdateChecker::checkForUpdates(): GET" << apiUrl;
    requestInFlight = true;
    networkManager->get(request);
}

QString UpdateChecker::normalizeVersionTag(const QString &tag)
{
    QString normalized = tag.trimmed();
    if (normalized.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) {
        normalized.remove(0, 1);
    }
    return normalized.trimmed();
}

bool UpdateChecker::isNewerThan(const QString &latestTag, const QString &currentVersion)
{
    const QString latest = normalizeVersionTag(latestTag);
    const QString current = currentVersion.trimmed();

    if (latest.isEmpty() || current.isEmpty()) {
        return false;
    }

    const QVersionNumber latestVn = QVersionNumber::fromString(latest);
    const QVersionNumber currentVn = QVersionNumber::fromString(current);

    if (latestVn.isNull() || currentVn.isNull()) {
        return false;
    }

    return latestVn > currentVn;
}

void UpdateChecker::handleReply(QNetworkReply *reply)
{
    requestInFlight = false;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        const QString err = reply->errorString();
        tbcDebugStream() << "UpdateChecker::handleReply(): network error:" << err;
        emit checkFailed(tr("Network error: %1").arg(err));
        return;
    }

    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (statusCode != 200) {
        tbcDebugStream() << "UpdateChecker::handleReply(): unexpected HTTP status" << statusCode;
        emit checkFailed(tr("GitHub returned HTTP status %1.").arg(statusCode));
        return;
    }

    const QByteArray payload = reply->readAll();
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        tbcDebugStream() << "UpdateChecker::handleReply(): JSON parse error:"
                         << parseError.errorString();
        emit checkFailed(tr("Could not parse the release information from GitHub."));
        return;
    }

    const QJsonObject obj = doc.object();
    const QString tagName = obj.value(QStringLiteral("tag_name")).toString();
    const QString htmlUrl = obj.value(QStringLiteral("html_url")).toString();
    const QString releaseName = obj.value(QStringLiteral("name")).toString();

    if (tagName.isEmpty()) {
        emit checkFailed(tr("GitHub response did not include a release tag."));
        return;
    }

    const QString latestVersion = normalizeVersionTag(tagName);
    const QString currentVersion = QString::fromUtf8(APP_VERSION);
    const QString effectiveReleaseUrl = htmlUrl.isEmpty() ? releasesUrl() : htmlUrl;

    tbcDebugStream() << "UpdateChecker::handleReply(): latest =" << tagName
                     << " current =" << currentVersion
                     << " url =" << effectiveReleaseUrl;

    if (isNewerThan(tagName, currentVersion)) {
        emit updateAvailable(latestVersion, effectiveReleaseUrl, releaseName);
    } else {
        emit upToDate(currentVersion, latestVersion, effectiveReleaseUrl);
    }
}
