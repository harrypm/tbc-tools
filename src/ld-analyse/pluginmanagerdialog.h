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

class CudaPluginManager;
class QLabel;
class QPushButton;
class QProgressBar;
class QTextEdit;
class QListWidget;
class QListWidgetItem;

// A descriptor for a known plugin in the built-in registry.
struct PluginDescriptor {
    QString id;           // stable plugin id (e.g. "tbc-tools.cuda-runtime")
    QString displayName;  // shown in the plugin list
    QString description;  // shown in the details panel
    QString category;     // grouping label (e.g. "GPU acceleration")
};

class PluginManagerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PluginManagerDialog(QWidget *parent = nullptr);
    ~PluginManagerDialog() override;

private slots:
    void onPluginSelected(QListWidgetItem *current);
    void onCheckForUpdate();
    void onInstall();
    void onRemove();
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
    void setBusy(bool busy);
    void appendLog(const QString &message);

    // The built-in plugin registry (extensible).
    QList<PluginDescriptor> m_plugins;
    int m_selectedIndex = -1;  // index into m_plugins, or -1 if none selected

    CudaPluginManager *m_cudaManager;  // backend for the CUDA plugin

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
    QPushButton *m_removeButton;
    QPushButton *m_closeButton;
    QProgressBar *m_progressBar;
    QTextEdit *m_logView;

    QString m_latestVersion;
    QString m_latestReleaseTag;
    bool m_updateAvailable = false;
};

#endif // PLUGINMANAGERDIALOG_H
