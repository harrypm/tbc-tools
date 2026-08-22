/******************************************************************************
 * pluginmanagerdialog.h
 * ld-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Harry Munday
 *
 * This file is part of tbc-tools.
 *
 * PluginManagerDialog is the GUI front-end for tbc-tools' opt-in runtime
 * plugins. It presents a list of available plugins (from a built-in registry)
 * with per-plugin status / Install / Remove / Check-for-update controls, a
 * download progress bar, and a restart prompt after install/remove.
 *
 * The registry currently has one plugin (nnTransform3D CUDA runtime) but the
 * structure supports adding more: each entry has an id, display name,
 * description, and category, and the details panel adapts to the selected
 * plugin's backend manager.
 ******************************************************************************/

#ifndef PLUGINMANAGERDIALOG_H
#define PLUGINMANAGERDIALOG_H

#include <QDialog>
#include <QString>
#include <QList>

#include "plugincatalog.h"

class CudaPluginManager;
class GenericPluginInstaller;
class QLabel;
class QPushButton;
class QProgressBar;
class QTextEdit;
class QListWidget;
class QListWidgetItem;
class QShowEvent;

class PluginManagerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PluginManagerDialog(QWidget *parent = nullptr);
    ~PluginManagerDialog() override;

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void onPluginSelected(QListWidgetItem *current);
    void onCheckForUpdate();
    void onInstall();
    void onInstallFromLocalArchive();
    void onRemove();
    void onRetry();
    void onCancelDownload();
    void onCatalogFetched(const QList<PluginCatalogEntry> &entries);
    void onCatalogFetchFailed(const QString &error);
    void onLatestReleaseResolved(const QString &version, const QString &releaseTag, const QString &releaseUrl);
    void onReleaseCheckFailed(const QString &error);
    void onInstallProgress(qint64 bytesReceived, qint64 bytesTotal, const QString &currentFile);
    void onInstallSucceeded(const QString &installPath);
    void onInstallFailed(const QString &error);
    void onRemoveSucceeded();
    void onRemoveFailed(const QString &error);

private:
    void populatePluginList();
    void updateStatusDisplay();
    void setBusy(bool busy, bool canCancel = false);
    void appendLog(const QString &message);

    // The plugin catalog (bundled + cached + remote-fetched).
    QList<PluginCatalogEntry> m_plugins;
    int m_selectedIndex = -1;  // index into m_plugins, or -1 if none selected
    bool m_catalogFetchInProgress = false;
    bool m_installCancelled = false;  // set by Cancel download; consumed by onInstallFailed

    PluginCatalog *m_catalog;              // discovery (bundled/cached/remote)
    CudaPluginManager *m_cudaManager;      // backend for cuda-runtime plugins
    GenericPluginInstaller *m_genericInstaller;  // backend for generic plugins

    // UI
    QListWidget *m_pluginList;
    QLabel *m_nameLabel;
    QLabel *m_categoryLabel;
    QLabel *m_descriptionLabel;
    QLabel *m_statusLabel;
    QLabel *m_versionLabel;
    QLabel *m_platformLabel;
    QLabel *m_installPathLabel;
    QPushButton *m_checkButton;
    QPushButton *m_installButton;
    QPushButton *m_installFromArchiveButton;
    QPushButton *m_removeButton;
    QPushButton *m_closeButton;
    QPushButton *m_retryButton;
    QPushButton *m_cancelButton;
    QLabel *m_catalogStatusLabel;
    QProgressBar *m_progressBar;
    QTextEdit *m_logView;

    QString m_latestVersion;
    QString m_latestReleaseTag;
    bool m_updateAvailable = false;
};

#endif // PLUGINMANAGERDIALOG_H
