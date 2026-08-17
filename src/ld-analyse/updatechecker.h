/******************************************************************************
 * updatechecker.h
 * ld-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-lter
 * SPDX-FileCopyrightText: 2026 Harry Munday
 *
 * This file is part of tbc-tools.
 *
 * UpdateChecker performs an asynchronous HTTP GET against the GitHub
 * "latest release" REST endpoint for the tbc-tools repository, parses the
 * JSON response (tag_name / html_url / name) and compares the published
 * tag against the compiled-in APP_VERSION.
 ******************************************************************************/

#ifndef UPDATECHECKER_H
#define UPDATECHECKER_H

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>

class QNetworkReply;

class UpdateChecker : public QObject
{
    Q_OBJECT

public:
    explicit UpdateChecker(QObject *parent = nullptr);
    ~UpdateChecker() override;

    // The repository whose latest release we query.
    static const QString repositoryOwner;
    static const QString repositoryName;

    // Human-readable release index URL (https://github.com/<owner>/<name>/releases/latest).
    static QString releasesUrl();

    // Kick off an asynchronous "latest release" lookup. Results are delivered
    // via updateAvailable / upToDate / checkFailed. Safe to call again once a
    // previous reply has finished; concurrent in-flight requests are ignored.
    void checkForUpdates();

    // Strip a leading 'v'/'V' from a tag like "v3.2.6" -> "3.2.6".
    static QString normalizeVersionTag(const QString &tag);

    // Returns true when latestTag (e.g. "v3.2.6") is strictly newer than
    // currentVersion (e.g. "3.2.6"). Returns false if either side is empty or
    // unparseable, so callers can treat ambiguous results as "no update".
    static bool isNewerThan(const QString &latestTag, const QString &currentVersion);

signals:
    // A newer release was published. latestVersion is normalised (no 'v').
    void updateAvailable(const QString &latestVersion, const QString &releaseUrl, const QString &releaseName);
    // The latest published release is not newer than currentVersion.
    void upToDate(const QString &currentVersion, const QString &latestVersion, const QString &releaseUrl);
    // The check could not complete (network error, non-200 response, bad JSON).
    void checkFailed(const QString &errorString);

private slots:
    void handleReply(QNetworkReply *reply);

private:
    QNetworkAccessManager *networkManager;
    bool requestInFlight = false;
};

#endif // UPDATECHECKER_H
