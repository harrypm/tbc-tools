/******************************************************************************
 * pluginmanagerdialog.cpp
 * ld-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Harry Munday
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#include "pluginmanagerdialog.h"
#include "cudapluginmanager.h"
#include "configuration.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QTextEdit>
#include <QMessageBox>
#include <QHeaderView>
#include <QVersionNumber>

PluginManagerDialog::PluginManagerDialog(QWidget *parent)
    : QDialog(parent),
      m_manager(new CudaPluginManager(this))
{
    setWindowTitle(tr("CUDA Plugin Manager"));
    setMinimumWidth(560);
    setMinimumHeight(420);

    auto *mainLayout = new QVBoxLayout(this);

    // --- Status section ---
    auto *formLayout = new QFormLayout();
    m_statusLabel = new QLabel(this);
    m_versionLabel = new QLabel(this);
    m_platformLabel = new QLabel(this);
    m_installPathLabel = new QLabel(this);
    m_installPathLabel->setWordWrap(true);
    formLayout->addRow(tr("Status:"), m_statusLabel);
    formLayout->addRow(tr("Installed version:"), m_versionLabel);
    formLayout->addRow(tr("Platform:"), m_platformLabel);
    formLayout->addRow(tr("Install path:"), m_installPathLabel);
    mainLayout->addLayout(formLayout);

    // --- Buttons ---
    auto *buttonLayout = new QHBoxLayout();
    m_checkButton = new QPushButton(tr("Check for update"), this);
    m_installButton = new QPushButton(tr("Install / Update"), this);
    m_removeButton = new QPushButton(tr("Remove"), this);
    m_closeButton = new QPushButton(tr("Close"), this);
    buttonLayout->addWidget(m_checkButton);
    buttonLayout->addWidget(m_installButton);
    buttonLayout->addWidget(m_removeButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_closeButton);
    mainLayout->addLayout(buttonLayout);

    // --- Progress ---
    m_progressBar = new QProgressBar(this);
    m_progressBar->setVisible(false);
    mainLayout->addWidget(m_progressBar);

    // --- Log ---
    m_logView = new QTextEdit(this);
    m_logView->setReadOnly(true);
    m_logView->setPlaceholderText(tr("Plugin manager log..."));
    mainLayout->addWidget(m_logView, 1);

    // --- Connections ---
    connect(m_checkButton, &QPushButton::clicked, this, &PluginManagerDialog::onCheckForUpdate);
    connect(m_installButton, &QPushButton::clicked, this, &PluginManagerDialog::onInstall);
    connect(m_removeButton, &QPushButton::clicked, this, &PluginManagerDialog::onRemove);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);

    connect(m_manager, &CudaPluginManager::latestReleaseResolved,
            this, &PluginManagerDialog::onLatestReleaseResolved);
    connect(m_manager, &CudaPluginManager::releaseCheckFailed,
            this, &PluginManagerDialog::onReleaseCheckFailed);
    connect(m_manager, &CudaPluginManager::installProgress,
            this, &PluginManagerDialog::onInstallProgress);
    connect(m_manager, &CudaPluginManager::installSucceeded,
            this, &PluginManagerDialog::onInstallSucceeded);
    connect(m_manager, &CudaPluginManager::installFailed,
            this, &PluginManagerDialog::onInstallFailed);
    connect(m_manager, &CudaPluginManager::removeSucceeded,
            this, &PluginManagerDialog::onRemoveSucceeded);
    connect(m_manager, &CudaPluginManager::removeFailed,
            this, &PluginManagerDialog::onRemoveFailed);

    m_platformLabel->setText(QStringLiteral("%1-%2")
        .arg(CudaPluginManager::currentPlatform(), CudaPluginManager::currentArch()));

    updateStatusDisplay();
}

PluginManagerDialog::~PluginManagerDialog() = default;

void PluginManagerDialog::updateStatusDisplay()
{
    Configuration c;
    const QString installedVersion = c.getCudaPluginInstalledVersion();
    const QString installPath = c.getCudaPluginInstallPath();

    if (installedVersion.isEmpty() || installPath.isEmpty() || !QDir(installPath).exists()) {
        m_statusLabel->setText(tr("Not installed"));
        m_versionLabel->setText(tr("—"));
        m_installPathLabel->setText(tr("—"));
        m_removeButton->setEnabled(false);
        if (m_latestVersion.isEmpty()) {
            m_installButton->setEnabled(false);
            m_installButton->setText(tr("Install / Update"));
        } else {
            m_installButton->setEnabled(true);
            m_installButton->setText(tr("Install v%1").arg(m_latestVersion));
        }
    } else {
        m_statusLabel->setText(m_updateAvailable
            ? tr("Update available")
            : tr("Installed"));
        m_versionLabel->setText(installedVersion);
        m_installPathLabel->setText(installPath);
        m_removeButton->setEnabled(true);
        if (m_updateAvailable && !m_latestVersion.isEmpty()) {
            m_installButton->setEnabled(true);
            m_installButton->setText(tr("Update to v%1").arg(m_latestVersion));
        } else {
            m_installButton->setEnabled(false);
            m_installButton->setText(tr("Up to date"));
        }
    }
}

void PluginManagerDialog::setBusy(bool busy)
{
    m_checkButton->setEnabled(!busy);
    m_installButton->setEnabled(!busy && (!m_latestVersion.isEmpty() || m_updateAvailable));
    m_removeButton->setEnabled(!busy && !Configuration().getCudaPluginInstalledVersion().isEmpty());
    m_progressBar->setVisible(busy);
    if (!busy) {
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(0);
    }
}

void PluginManagerDialog::onCheckForUpdate()
{
    m_logView->append(tr("Checking for CUDA plugin updates..."));
    setBusy(true);
    m_manager->checkForUpdate();
}

void PluginManagerDialog::onLatestReleaseResolved(const QString &version, const QString &releaseTag, const QString &releaseUrl)
{
    Q_UNUSED(releaseUrl)
    m_latestVersion = version;
    m_latestReleaseTag = releaseTag;
    m_logView->append(tr("Latest CUDA plugin: v%1 (release %2)").arg(version, releaseTag));

    Configuration c;
    const QString installed = c.getCudaPluginInstalledVersion();
    m_updateAvailable = !installed.isEmpty()
        && QVersionNumber::fromString(version) > QVersionNumber::fromString(installed);

    if (installed.isEmpty()) {
        m_logView->append(tr("No CUDA plugin installed. Click Install to download v%1.").arg(version));
    } else if (m_updateAvailable) {
        m_logView->append(tr("Update available: v%1 -> v%2.").arg(installed, version));
    } else {
        m_logView->append(tr("Installed version v%1 is up to date.").arg(installed));
    }

    setBusy(false);
    updateStatusDisplay();
}

void PluginManagerDialog::onReleaseCheckFailed(const QString &error)
{
    m_logView->append(tr("Check failed: %1").arg(error));
    setBusy(false);
}

void PluginManagerDialog::onInstall()
{
    if (m_latestVersion.isEmpty()) {
        QMessageBox::information(this, tr("No update info"),
            tr("Click \"Check for update\" first to resolve the latest CUDA plugin version."));
        return;
    }

    // Trust confirmation (decode-orc pattern): each install is a new binary
    // downloaded from the internet, so ask the user to confirm.
    auto confirm = QMessageBox::question(this, tr("Install CUDA plugin"),
        tr("This will download the CUDA 11.8 + cuDNN 8.9 runtime package "
           "(~1.3 GB) from harrypm/tbc-tools-ci-cache and install it to:\n\n%1\n\n"
           "The package is SHA-256 verified but NOT code-signed. Continue?")
            .arg(CudaPluginManager::defaultInstallDirectory()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (confirm != QMessageBox::Yes) {
        return;
    }

    m_logView->append(tr("Downloading and installing CUDA plugin v%1...").arg(m_latestVersion));
    setBusy(true);
    m_progressBar->setRange(0, 0); // indeterminate during manifest download
    m_manager->downloadAndInstall();
}

void PluginManagerDialog::onInstallProgress(qint64 bytesReceived, qint64 bytesTotal, const QString &currentFile)
{
    if (bytesTotal > 0) {
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(static_cast<int>((bytesReceived * 100) / bytesTotal));
    } else {
        m_progressBar->setRange(0, 0); // indeterminate
    }
    if (!currentFile.isEmpty() && bytesReceived == 0) {
        m_logView->append(tr("Downloading %1...").arg(currentFile));
    }
}

void PluginManagerDialog::onInstallSucceeded(const QString &installPath)
{
    m_logView->append(tr("CUDA plugin installed successfully to %1").arg(installPath));
    m_logView->append(tr("Restart ld-analyse for the CUDA EP to be picked up by nnTransform3D."));
    setBusy(false);
    updateStatusDisplay();
    QMessageBox::information(this, tr("Install complete"),
        tr("The CUDA plugin was installed successfully.\n\n"
           "Restart ld-analyse for the CUDA execution provider to be loaded "
           "by nnTransform3D."));
}

void PluginManagerDialog::onInstallFailed(const QString &error)
{
    m_logView->append(tr("Install failed: %1").arg(error));
    setBusy(false);
    QMessageBox::warning(this, tr("Install failed"), error);
}

void PluginManagerDialog::onRemove()
{
    auto confirm = QMessageBox::question(this, tr("Remove CUDA plugin"),
        tr("This will delete the installed CUDA runtime plugin.\n\n"
           "Restart ld-analyse after removal for the change to take effect.\n\nContinue?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (confirm != QMessageBox::Yes) {
        return;
    }
    m_logView->append(tr("Removing CUDA plugin..."));
    setBusy(true);
    m_manager->remove();
}

void PluginManagerDialog::onRemoveSucceeded()
{
    m_logView->append(tr("CUDA plugin removed. Restart ld-analyse for the change to take effect."));
    setBusy(false);
    updateStatusDisplay();
    QMessageBox::information(this, tr("Removed"),
        tr("The CUDA plugin was removed.\n\n"
           "Restart ld-analyse for the change to take effect."));
}

void PluginManagerDialog::onRemoveFailed(const QString &error)
{
    m_logView->append(tr("Remove failed: %1").arg(error));
    setBusy(false);
    QMessageBox::warning(this, tr("Remove failed"), error);
}
