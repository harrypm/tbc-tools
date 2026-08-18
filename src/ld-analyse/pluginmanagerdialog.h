/******************************************************************************
 * pluginmanagerdialog.h
 * ld-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Harry Munday
 *
 * This file is part of tbc-tools.
 *
 * PluginManagerDialog is the GUI front-end for the opt-in CUDA runtime plugin.
 * It wraps CudaPluginManager (the non-GUI lifecycle logic) and presents
 * status / Install / Remove / Check-for-update controls, a download progress
 * bar, and a restart prompt after install/remove (the ORT CUDA EP is resolved
 * at nnTransform3D session-creation, so the next decode picks it up, but a
 * restart is recommended).
 ******************************************************************************/

#ifndef PLUGINMANAGERDIALOG_H
#define PLUGINMANAGERDIALOG_H

#include <QDialog>
#include <QString>

class CudaPluginManager;
class QLabel;
class QPushButton;
class QProgressBar;
class QTextEdit;

class PluginManagerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PluginManagerDialog(QWidget *parent = nullptr);
    ~PluginManagerDialog() override;

private slots:
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
    void updateStatusDisplay();
    void setBusy(bool busy);

    CudaPluginManager *m_manager;
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
