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
#include "plugincatalog.h"
#include "genericplugininstaller.h"
#include "configuration.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QSplitter>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QTextEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QVersionNumber>
#include <QFileDialog>
#include <QDir>
#include <QShowEvent>

PluginManagerDialog::PluginManagerDialog(QWidget *parent)
    : QDialog(parent),
      m_catalog(new PluginCatalog(this)),
      m_cudaManager(new CudaPluginManager(this)),
      m_genericInstaller(new GenericPluginInstaller(this))
{
    setWindowTitle(tr("Plugin Manager"));
    setMinimumWidth(720);
    setMinimumHeight(480);

    // --- Plugin catalog (bundled + cached; refreshed from remote on show) ---
    // Start from the bundled + cached catalog so the list is never empty;
    // showEvent() fetches the latest remote catalog every time the dialog opens.
    m_catalog->loadBundled();
    m_catalog->loadCached();
    m_plugins = m_catalog->entries();

    auto *mainLayout = new QVBoxLayout(this);

    // --- Catalog status + retry (above the splitter) ---
    m_catalogStatusLabel = new QLabel(tr("Fetching catalog..."), this);
    mainLayout->addWidget(m_catalogStatusLabel);

    auto *retryLayout = new QHBoxLayout();
    m_retryButton = new QPushButton(tr("Retry fetch"), this);
    m_retryButton->setVisible(false);
    retryLayout->addWidget(m_retryButton);
    retryLayout->addStretch();
    mainLayout->addLayout(retryLayout);

    // --- Splitter: plugin list (left) + details (right) ---
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    mainLayout->addWidget(splitter, 1);

    // Left: plugin selection list.
    auto *leftWidget = new QWidget(this);
    auto *leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    auto *listHeader = new QLabel(tr("Available plugins:"), leftWidget);
    leftLayout->addWidget(listHeader);
    m_pluginList = new QListWidget(leftWidget);
    leftLayout->addWidget(m_pluginList, 1);
    splitter->addWidget(leftWidget);

    // Right: details panel for the selected plugin.
    auto *rightWidget = new QWidget(this);
    auto *rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    m_nameLabel = new QLabel(rightWidget);
    QFont nameFont = m_nameLabel->font();
    nameFont.setBold(true);
    nameFont.setPointSize(nameFont.pointSize() + 2);
    m_nameLabel->setFont(nameFont);
    rightLayout->addWidget(m_nameLabel);

    m_categoryLabel = new QLabel(rightWidget);
    m_categoryLabel->setStyleSheet(QStringLiteral("color: gray;"));
    rightLayout->addWidget(m_categoryLabel);

    m_descriptionLabel = new QLabel(rightWidget);
    m_descriptionLabel->setWordWrap(true);
    rightLayout->addWidget(m_descriptionLabel);

    auto *formLayout = new QFormLayout();
    m_statusLabel = new QLabel(rightWidget);
    m_versionLabel = new QLabel(rightWidget);
    m_platformLabel = new QLabel(rightWidget);
    m_installPathLabel = new QLabel(rightWidget);
    m_installPathLabel->setWordWrap(true);
    formLayout->addRow(tr("Status:"), m_statusLabel);
    formLayout->addRow(tr("Installed version:"), m_versionLabel);
    formLayout->addRow(tr("Platform:"), m_platformLabel);
    formLayout->addRow(tr("Install path:"), m_installPathLabel);
    rightLayout->addLayout(formLayout);

    auto *buttonLayout = new QHBoxLayout();
    m_checkButton = new QPushButton(tr("Check for update"), rightWidget);
    m_installButton = new QPushButton(tr("Install / Update"), rightWidget);
    m_installFromArchiveButton = new QPushButton(tr("Install from local archive"), rightWidget);
    m_removeButton = new QPushButton(tr("Remove"), rightWidget);
    buttonLayout->addWidget(m_checkButton);
    buttonLayout->addWidget(m_installButton);
    buttonLayout->addWidget(m_installFromArchiveButton);
    buttonLayout->addWidget(m_removeButton);
    buttonLayout->addStretch();
    rightLayout->addLayout(buttonLayout);

    rightLayout->addStretch();

    splitter->addWidget(rightWidget);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);

    // --- Progress + cancel + log (below the splitter) ---
    auto *progressLayout = new QHBoxLayout();
    m_progressBar = new QProgressBar(this);
    m_progressBar->setVisible(false);
    progressLayout->addWidget(m_progressBar, 1);
    m_cancelButton = new QPushButton(tr("Cancel download"), this);
    m_cancelButton->setVisible(false);
    progressLayout->addWidget(m_cancelButton);
    mainLayout->addLayout(progressLayout);

    m_logView = new QTextEdit(this);
    m_logView->setReadOnly(true);
    m_logView->setPlaceholderText(tr("Plugin manager log..."));
    m_logView->setMaximumHeight(120);
    mainLayout->addWidget(m_logView);

    // --- Close button ---
    auto *closeLayout = new QHBoxLayout();
    closeLayout->addStretch();
    m_closeButton = new QPushButton(tr("Close"), this);
    closeLayout->addWidget(m_closeButton);
    mainLayout->addLayout(closeLayout);

    // --- Connections ---
    connect(m_pluginList, &QListWidget::currentItemChanged, this, &PluginManagerDialog::onPluginSelected);
    connect(m_checkButton, &QPushButton::clicked, this, &PluginManagerDialog::onCheckForUpdate);
    connect(m_installButton, &QPushButton::clicked, this, &PluginManagerDialog::onInstall);
    connect(m_installFromArchiveButton, &QPushButton::clicked,
            this, &PluginManagerDialog::onInstallFromLocalArchive);
    connect(m_removeButton, &QPushButton::clicked, this, &PluginManagerDialog::onRemove);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);

    connect(m_cudaManager, &CudaPluginManager::latestReleaseResolved,
            this, &PluginManagerDialog::onLatestReleaseResolved);
    connect(m_cudaManager, &CudaPluginManager::releaseCheckFailed,
            this, &PluginManagerDialog::onReleaseCheckFailed);
    connect(m_cudaManager, &CudaPluginManager::installProgress,
            this, &PluginManagerDialog::onInstallProgress);
    connect(m_cudaManager, &CudaPluginManager::installSucceeded,
            this, &PluginManagerDialog::onInstallSucceeded);
    connect(m_cudaManager, &CudaPluginManager::installFailed,
            this, &PluginManagerDialog::onInstallFailed);
    connect(m_cudaManager, &CudaPluginManager::removeSucceeded,
            this, &PluginManagerDialog::onRemoveSucceeded);
    connect(m_cudaManager, &CudaPluginManager::removeFailed,
            this, &PluginManagerDialog::onRemoveFailed);

    // Catalog discovery + generic backend + retry wiring.
    connect(m_catalog, &PluginCatalog::catalogFetched,
            this, &PluginManagerDialog::onCatalogFetched);
    connect(m_catalog, &PluginCatalog::fetchFailed,
            this, &PluginManagerDialog::onCatalogFetchFailed);
    connect(m_genericInstaller, &GenericPluginInstaller::installProgress,
            this, &PluginManagerDialog::onInstallProgress);
    connect(m_genericInstaller, &GenericPluginInstaller::installSucceeded,
            this, &PluginManagerDialog::onInstallSucceeded);
    connect(m_genericInstaller, &GenericPluginInstaller::installFailed,
            this, &PluginManagerDialog::onInstallFailed);
    connect(m_genericInstaller, &GenericPluginInstaller::removeSucceeded,
            this, &PluginManagerDialog::onRemoveSucceeded);
    connect(m_genericInstaller, &GenericPluginInstaller::removeFailed,
            this, &PluginManagerDialog::onRemoveFailed);
    connect(m_retryButton, &QPushButton::clicked, this, &PluginManagerDialog::onRetry);
    connect(m_cancelButton, &QPushButton::clicked, this, &PluginManagerDialog::onCancelDownload);

    m_platformLabel->setText(QStringLiteral("%1-%2")
        .arg(CudaPluginManager::currentPlatform(), CudaPluginManager::currentArch()));

    populatePluginList();
    // Select the first plugin by default.
    if (!m_plugins.isEmpty()) {
        m_pluginList->setCurrentRow(0);
    }
}

PluginManagerDialog::~PluginManagerDialog() = default;

void PluginManagerDialog::populatePluginList()
{
    m_pluginList->clear();
    for (const PluginCatalogEntry &plugin : m_plugins) {
        auto *item = new QListWidgetItem(plugin.displayName, m_pluginList);
        item->setData(Qt::UserRole, plugin.id);
        item->setToolTip(plugin.description);
    }
}

void PluginManagerDialog::appendLog(const QString &message)
{
    m_logView->append(message);
}

void PluginManagerDialog::onPluginSelected(QListWidgetItem *current)
{
    if (current == nullptr) {
        m_selectedIndex = -1;
        m_nameLabel->clear();
        m_categoryLabel->clear();
        m_descriptionLabel->clear();
        m_checkButton->setEnabled(false);
        m_installButton->setEnabled(false);
        m_installFromArchiveButton->setEnabled(false);
        m_removeButton->setEnabled(false);
        return;
    }

    m_selectedIndex = m_pluginList->row(current);
    const PluginCatalogEntry &plugin = m_plugins.at(m_selectedIndex);
    m_nameLabel->setText(plugin.displayName);
    m_categoryLabel->setText(plugin.category);
    m_descriptionLabel->setText(plugin.description);
    // Reset resolved-update state for the newly selected plugin.
    m_latestVersion.clear();
    m_latestReleaseTag.clear();
    m_updateAvailable = false;
    // Generic plugins know their latest version from the catalog directly;
    // cuda-runtime resolves it via Check for update.
    if (plugin.backend == QStringLiteral("generic")) {
        m_latestVersion = plugin.version;
    }
    updateStatusDisplay();
}

void PluginManagerDialog::updateStatusDisplay()
{
    if (m_selectedIndex < 0) {
        m_statusLabel->setText(tr("—"));
        m_versionLabel->setText(tr("—"));
        m_installPathLabel->setText(tr("—"));
        m_checkButton->setEnabled(false);
        m_installButton->setEnabled(false);
        m_installFromArchiveButton->setEnabled(false);
        m_removeButton->setEnabled(false);
        return;
    }

    const PluginCatalogEntry &plugin = m_plugins.at(m_selectedIndex);
    QString installedVersion;
    QString installPath;

    if (plugin.backend == QStringLiteral("cuda-runtime")) {
        Configuration c;
        installedVersion = c.getCudaPluginInstalledVersion();
        installPath = c.getCudaPluginInstallPath();
    } else if (plugin.backend == QStringLiteral("generic")) {
        const GenericPluginInstalledInfo info = m_genericInstaller->installedInfo(plugin.id);
        installedVersion = info.installed ? info.version : QString();
        installPath = info.installPath;
    } else {
        m_statusLabel->setText(tr("Unsupported backend"));
        m_versionLabel->setText(tr("—"));
        m_installPathLabel->setText(tr("—"));
        m_checkButton->setEnabled(false);
        m_installButton->setEnabled(false);
        m_installButton->setText(tr("Install / Update"));
        m_installFromArchiveButton->setEnabled(false);
        m_removeButton->setEnabled(false);
        return;
    }

    const bool present = !installedVersion.isEmpty()
        && !installPath.isEmpty()
        && QDir(installPath).exists();

    if (!present) {
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

    // Install-from-local-archive is only wired for the cuda-runtime backend.
    m_installFromArchiveButton->setEnabled(plugin.backend == QStringLiteral("cuda-runtime"));
    m_checkButton->setEnabled(true);
}

void PluginManagerDialog::setBusy(bool busy, bool canCancel)
{
    if (busy) {
        m_checkButton->setEnabled(false);
        m_installButton->setEnabled(false);
        m_installFromArchiveButton->setEnabled(false);
        m_removeButton->setEnabled(false);
        m_progressBar->setVisible(true);
        m_cancelButton->setVisible(canCancel);
        m_cancelButton->setEnabled(canCancel);
    } else {
        m_progressBar->setVisible(false);
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(0);
        m_cancelButton->setVisible(false);
        updateStatusDisplay();
    }
}

void PluginManagerDialog::onCheckForUpdate()
{
    if (m_selectedIndex < 0) {
        return;
    }
    const PluginCatalogEntry &plugin = m_plugins.at(m_selectedIndex);
    appendLog(tr("Checking for %1 updates...").arg(plugin.displayName));
    if (plugin.backend == QStringLiteral("cuda-runtime")) {
        setBusy(true);
        m_cudaManager->checkForUpdate();
    } else if (plugin.backend == QStringLiteral("generic")) {
        // Generic update detection is local: compare the installed record's
        // version against the catalog version (no network round-trip).
        const GenericPluginInstalledInfo info = m_genericInstaller->installedInfo(plugin.id);
        m_latestVersion = plugin.version;
        if (!info.installed) {
            m_updateAvailable = false;
            appendLog(tr("Not installed. Latest available: v%1.").arg(plugin.version));
        } else {
            const bool newer = !plugin.version.isEmpty()
                && QVersionNumber::fromString(plugin.version) > QVersionNumber::fromString(info.version);
            m_updateAvailable = newer;
            if (newer) {
                appendLog(tr("Update available: v%1 -> v%2.").arg(info.version, plugin.version));
            } else {
                appendLog(tr("Installed v%1 is up to date.").arg(info.version));
            }
        }
        updateStatusDisplay();
    } else {
        appendLog(tr("No backend wired for plugin %1.").arg(plugin.id));
    }
}

void PluginManagerDialog::onLatestReleaseResolved(const QString &version, const QString &releaseTag, const QString &releaseUrl)
{
    Q_UNUSED(releaseUrl)
    m_latestVersion = version;
    m_latestReleaseTag = releaseTag;
    appendLog(tr("Latest: v%1 (release %2)").arg(version, releaseTag));

    Configuration c;
    const QString installed = c.getCudaPluginInstalledVersion();
    m_updateAvailable = !installed.isEmpty()
        && QVersionNumber::fromString(version) > QVersionNumber::fromString(installed);

    if (installed.isEmpty()) {
        appendLog(tr("Not installed. Click Install to download v%1.").arg(version));
    } else if (m_updateAvailable) {
        appendLog(tr("Update available: v%1 -> v%2.").arg(installed, version));
    } else {
        appendLog(tr("Installed v%1 is up to date.").arg(installed));
    }

    setBusy(false);
    updateStatusDisplay();
}

void PluginManagerDialog::onReleaseCheckFailed(const QString &error)
{
    appendLog(tr("Check failed: %1").arg(error));
    setBusy(false);
}

void PluginManagerDialog::onInstall()
{
    if (m_selectedIndex < 0) {
        return;
    }
    const PluginCatalogEntry &plugin = m_plugins.at(m_selectedIndex);
    m_installCancelled = false;

    if (plugin.backend == QStringLiteral("cuda-runtime")) {
        if (m_latestVersion.isEmpty()) {
            QMessageBox::information(this, tr("No update info"),
                tr("Click \"Check for update\" first to resolve the latest version."));
            return;
        }
        auto confirm = QMessageBox::question(this, tr("Install %1").arg(plugin.displayName),
            tr("This will download the %1 package from harrypm/tbc-tools-ci-cache and "
               "install it to:\n\n%2\n\n"
               "The package is SHA-256 verified but NOT code-signed. Continue?")
                .arg(plugin.displayName, CudaPluginManager::defaultInstallDirectory()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (confirm != QMessageBox::Yes) {
            return;
        }
        appendLog(tr("Downloading and installing %1 v%2...").arg(plugin.displayName, m_latestVersion));
        setBusy(true, true);
        m_progressBar->setRange(0, 0); // indeterminate during manifest download
        m_cudaManager->downloadAndInstall();
    } else if (plugin.backend == QStringLiteral("generic")) {
        const QString installDir = QDir(PluginCatalog::pluginsRootDirectory()).filePath(plugin.id);
        auto confirm = QMessageBox::question(this, tr("Install %1").arg(plugin.displayName),
            tr("This will download the %1 package and install it to:\n\n%2\n\n"
               "The package is SHA-256 verified but NOT code-signed. Continue?")
                .arg(plugin.displayName, QDir::toNativeSeparators(installDir)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (confirm != QMessageBox::Yes) {
            return;
        }
        appendLog(tr("Downloading and installing %1 v%2...").arg(plugin.displayName, plugin.version));
        setBusy(true, true);
        m_progressBar->setRange(0, 0); // indeterminate until downloadProgress arrives
        m_genericInstaller->install(plugin);
    } else {
        appendLog(tr("No backend wired for plugin %1.").arg(plugin.id));
    }
}

void PluginManagerDialog::onInstallFromLocalArchive()
{
    if (m_selectedIndex < 0) {
        return;
    }
    const PluginCatalogEntry &plugin = m_plugins.at(m_selectedIndex);
    if (plugin.backend != QStringLiteral("cuda-runtime")) {
        appendLog(tr("Install from local archive is not available for plugin %1.").arg(plugin.id));
        return;
    }

    const QString archivePath = QFileDialog::getOpenFileName(
        this,
        tr("Select local CUDA plugin archive"),
        QDir::homePath(),
        tr("Plugin archives (*.zip *.tar.gz *.tgz);;All files (*)"));
    if (archivePath.isEmpty()) {
        return;
    }

    auto confirm = QMessageBox::question(
        this, tr("Install %1 from local archive").arg(plugin.displayName),
        tr("This will install %1 from:\n\n%2\n\n"
           "A matching manifest JSON must be present in the same directory, and "
           "all files are SHA-256 verified before activation. Continue?")
            .arg(plugin.displayName, QDir::toNativeSeparators(archivePath)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (confirm != QMessageBox::Yes) {
        return;
    }

    appendLog(tr("Installing %1 from local archive: %2")
                  .arg(plugin.displayName, QDir::toNativeSeparators(archivePath)));
    setBusy(true);
    m_progressBar->setRange(0, 0);
    m_cudaManager->installFromLocalArchive(archivePath);
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
        appendLog(tr("Downloading %1...").arg(currentFile));
    }
}

void PluginManagerDialog::onInstallSucceeded(const QString &installPath)
{
    m_installCancelled = false;
    appendLog(tr("Installed successfully to %1").arg(installPath));
    appendLog(tr("Restart ld-analyse for the change to take effect."));
    setBusy(false);
    updateStatusDisplay();
    QMessageBox::information(this, tr("Install complete"),
        tr("The plugin was installed successfully.\n\n"
           "Restart ld-analyse for the change to take effect."));
}

void PluginManagerDialog::onInstallFailed(const QString &error)
{
    if (m_installCancelled) {
        appendLog(tr("Download cancelled."));
        m_installCancelled = false;
        setBusy(false);
        QMessageBox::information(this, tr("Cancelled"), tr("The download was cancelled."));
        return;
    }
    appendLog(tr("Install failed: %1").arg(error));
    setBusy(false);
    QMessageBox::warning(this, tr("Install failed"), error);
}

void PluginManagerDialog::onRemove()
{
    if (m_selectedIndex < 0) {
        return;
    }
    const PluginCatalogEntry &plugin = m_plugins.at(m_selectedIndex);
    auto confirm = QMessageBox::question(this, tr("Remove %1").arg(plugin.displayName),
        tr("This will delete the installed %1.\n\n"
           "Restart ld-analyse after removal for the change to take effect.\n\nContinue?")
            .arg(plugin.displayName),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (confirm != QMessageBox::Yes) {
        return;
    }
    appendLog(tr("Removing %1...").arg(plugin.displayName));
    setBusy(true);
    if (plugin.backend == QStringLiteral("cuda-runtime")) {
        m_cudaManager->remove();
    } else if (plugin.backend == QStringLiteral("generic")) {
        m_genericInstaller->remove(plugin.id);
    } else {
        appendLog(tr("No backend wired for plugin %1.").arg(plugin.id));
        setBusy(false);
    }
}

void PluginManagerDialog::onRemoveSucceeded()
{
    appendLog(tr("Removed. Restart ld-analyse for the change to take effect."));
    setBusy(false);
    updateStatusDisplay();
    QMessageBox::information(this, tr("Removed"),
        tr("The plugin was removed.\n\n"
           "Restart ld-analyse for the change to take effect."));
}

void PluginManagerDialog::onRemoveFailed(const QString &error)
{
    appendLog(tr("Remove failed: %1").arg(error));
    setBusy(false);
    QMessageBox::warning(this, tr("Remove failed"), error);
}

// Pull the latest remote catalog every time the dialog is shown.
void PluginManagerDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    if (!m_catalogFetchInProgress) {
        appendLog(tr("Fetching plugin catalog..."));
        m_catalogStatusLabel->setText(tr("Fetching catalog..."));
        m_retryButton->setVisible(false);
        m_catalogFetchInProgress = true;
        m_catalog->fetchRemote();
    }
}

void PluginManagerDialog::onRetry()
{
    appendLog(tr("Retrying catalog fetch..."));
    m_catalogStatusLabel->setText(tr("Fetching catalog..."));
    m_retryButton->setVisible(false);
    m_catalogFetchInProgress = true;
    m_catalog->fetchRemote();
}

void PluginManagerDialog::onCancelDownload()
{
    if (m_selectedIndex < 0) {
        return;
    }
    const PluginCatalogEntry &plugin = m_plugins.at(m_selectedIndex);
    appendLog(tr("Cancelling download..."));
    m_installCancelled = true;
    m_cancelButton->setEnabled(false);
    if (plugin.backend == QStringLiteral("cuda-runtime")) {
        m_cudaManager->cancelInstall();
    } else if (plugin.backend == QStringLiteral("generic")) {
        m_genericInstaller->cancelInstall();
    }
}

void PluginManagerDialog::onCatalogFetched(const QList<PluginCatalogEntry> &entries)
{
    m_catalogFetchInProgress = false;
    m_plugins = entries;
    populatePluginList();
    m_catalogStatusLabel->setText(tr("Catalog updated."));
    m_retryButton->setVisible(false);
    appendLog(tr("Catalog updated (%1 plugin(s)).").arg(entries.size()));

    // Preserve the current selection if still present, else select the first row.
    const int prevRow = m_selectedIndex;
    if (prevRow >= 0 && prevRow < m_plugins.size()) {
        m_pluginList->setCurrentRow(prevRow);
    } else if (!m_plugins.isEmpty()) {
        m_pluginList->setCurrentRow(0);
    } else {
        onPluginSelected(nullptr);
    }
}

void PluginManagerDialog::onCatalogFetchFailed(const QString &error)
{
    m_catalogFetchInProgress = false;
    m_catalogStatusLabel->setText(tr("Couldn't fetch catalog — showing bundled/cached list."));
    m_retryButton->setVisible(true);
    appendLog(tr("Catalog fetch failed: %1").arg(error));
}
