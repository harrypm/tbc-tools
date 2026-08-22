/******************************************************************************
 * plugincatalog.h
 * ld-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Harry Munday
 *
 * This file is part of tbc-tools.
 *
 * PluginCatalog owns plugin discovery for the Plugin Manager. It parses the
 * bundled catalog (Qt resource :/plugins/catalog.json), the last-good cached
 * remote catalog on disk, and asynchronously fetches the latest remote
 * catalog (jsDelivr CDN first, raw.githubusercontent.com fallback). On a
 * successful fetch it emits catalogFetched with the validated entries; on
 * failure it emits fetchFailed so the dialog can keep showing the
 * bundled/cached list and offer a Retry.
 *
 * PluginCatalogEntry is the shared descriptor consumed by both
 * PluginManagerDialog (for routing/display) and GenericPluginInstaller (for
 * the generic backend install/verify lifecycle). See docs/plugins.md.
 ******************************************************************************/

#ifndef PLUGINCATALOG_H
#define PLUGINCATALOG_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QNetworkAccessManager>

class QNetworkReply;

// One file declared by a generic plugin's catalog entry for SHA-256 verify.
struct PluginCatalogFile {
    QString name;     // path as extracted by tar, relative to the install dir
    QString sha256;   // expected SHA-256, lowercase hex
};

// A validated plugin entry from the catalog. cuda-runtime entries only use
// the descriptor fields; generic entries also populate version/packageUrl/files.
struct PluginCatalogEntry {
    QString id;            // stable plugin id (e.g. "tbc-tools.cuda-runtime")
    QString displayName;   // shown in the plugin list
    QString description;   // shown in the details panel
    QString category;      // grouping label (e.g. "GPU acceleration")
    QString backend;       // "cuda-runtime" or "generic"
    QString homepage;      // optional info URL

    // generic-backend only:
    QString version;       // catalog-advertised version (update detection)
    QString packageUrl;    // direct download URL for the archive
    QList<PluginCatalogFile> files;  // per-file SHA-256 manifest
};

class PluginCatalog : public QObject
{
    Q_OBJECT

public:
    explicit PluginCatalog(QObject *parent = nullptr);
    ~PluginCatalog() override;

    // The GitHub repo that hosts plugins/catalog.json (used to build the
    // jsDelivr + raw.githubusercontent.com URLs).
    static const QString repositoryOwner;
    static const QString repositoryName;

    // Load the bundled catalog (Qt resource). Always available offline.
    // Returns true if parsed (an empty plugins[] list is still "parsed").
    bool loadBundled();

    // Load the last-good cached remote catalog from disk, if present.
    // Returns true if a valid cached catalog was loaded.
    bool loadCached();

    // The currently-loaded entry list (bundled, cached, or last-fetched).
    QList<PluginCatalogEntry> entries() const { return m_entries; }

    // Asynchronously fetch the latest remote catalog (jsDelivr first, then
    // raw.githubusercontent.com). Emits catalogFetched or fetchFailed.
    void fetchRemote();

    // Where the cached remote catalog is stored on disk.
    static QString cachedCatalogPath();

    // Shared install root for all plugins (OS-appropriate, update-persistent):
    //   Linux:   ~/.local/share/tbc-tools/plugins
    //   Windows: %LOCALAPPDATA%/tbc-tools/plugins
    //   macOS:   ~/Library/Application Support/tbc-tools/plugins
    // GenericPluginInstaller installs each plugin under <root>/<id>.
    static QString pluginsRootDirectory();

signals:
    void catalogFetched(const QList<PluginCatalogEntry> &entries);
    void fetchFailed(const QString &errorString);

private slots:
    void handleReply(QNetworkReply *reply);

private:
    // Parse a catalog JSON document into m_entries. Returns true on success.
    // On failure, error is filled and m_entries is left unchanged.
    bool parseCatalog(const QByteArray &payload, QString &error);

    // Persist the given raw payload as the cached remote catalog.
    void saveCached(const QByteArray &payload) const;

    QNetworkAccessManager *m_networkManager;
    QList<PluginCatalogEntry> m_entries;
    bool m_requestInFlight = false;
    // Index of the URL being tried (0 = jsDelivr, 1 = raw fallback).
    int m_fetchAttempt = 0;
    QStringList m_fetchUrls;
};

#endif // PLUGINCATALOG_H
