/******************************************************************************
 * teletextviewerdialog.cpp
 * tbc-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018-2026 Simon Inns
 * SPDX-FileCopyrightText: 2026 Harry Munday
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#include "teletextviewerdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QRegularExpression>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
namespace {
void appendUniqueCandidate(QStringList &candidates, const QString &candidate)
{
    if (candidate.isEmpty()) {
        return;
    }
    for (const QString &existing : candidates) {
        if (existing.compare(candidate, Qt::CaseInsensitive) == 0) {
            return;
        }
    }
    candidates.append(candidate);
}

QString normalizedTeletextBaseName(const QFileInfo &pathInfo)
{
    QString baseName = pathInfo.completeBaseName();
    QString baseNameLower = baseName.toLower();
    const QStringList suffixesToStrip = {
        QStringLiteral(".tbc"),
        QStringLiteral(".ytbc"),
        QStringLiteral(".ctbc"),
        QStringLiteral(".tbcy"),
        QStringLiteral(".tbcc")
    };
    for (const QString &suffix : suffixesToStrip) {
        if (!baseNameLower.endsWith(suffix)) {
            continue;
        }
        baseName.chop(suffix.size());
        break;
    }
    return baseName;
}

bool directoryContainsHtmlPages(const QString &directoryPath)
{
    if (directoryPath.trimmed().isEmpty()) {
        return false;
    }
    const QDir directory(directoryPath);
    if (!directory.exists()) {
        return false;
    }
    const QStringList htmlPages = directory.entryList(
        QStringList() << QStringLiteral("*.html"),
        QDir::Files,
        QDir::Name | QDir::IgnoreCase
    );
    return !htmlPages.isEmpty();
}

QString resolveTeletextDirectoryFromHint(const QString &pathHint)
{
    const QFileInfo pathInfo(pathHint);
    if (!pathInfo.exists()) {
        return QString();
    }

    if (pathInfo.isFile() && pathInfo.suffix().compare(QStringLiteral("html"), Qt::CaseInsensitive) == 0) {
        const QDir htmlDirectory(pathInfo.absolutePath());
        if (directoryContainsHtmlPages(htmlDirectory.absolutePath())) {
            return htmlDirectory.absolutePath();
        }
    }

    if (pathInfo.isDir() && directoryContainsHtmlPages(pathInfo.absoluteFilePath())) {
        return QDir(pathInfo.absoluteFilePath()).absolutePath();
    }

    QDir baseDirectory;
    if (pathInfo.isDir()) {
        baseDirectory = QDir(pathInfo.absoluteFilePath());
    } else {
        baseDirectory = pathInfo.absoluteDir();
    }

    QStringList candidateDirectories;
    QStringList baseNames;
    if (pathInfo.isDir()) {
        appendUniqueCandidate(baseNames, pathInfo.fileName());
    } else {
        appendUniqueCandidate(baseNames, normalizedTeletextBaseName(pathInfo));
        appendUniqueCandidate(baseNames, pathInfo.completeBaseName());
    }

    for (const QString &baseName : baseNames) {
        if (baseName.isEmpty()) {
            continue;
        }
        appendUniqueCandidate(candidateDirectories, baseDirectory.filePath(baseName + QStringLiteral("_teletext_html")));
        appendUniqueCandidate(candidateDirectories, baseDirectory.filePath(baseName + QStringLiteral(".teletext_html")));
        appendUniqueCandidate(candidateDirectories, baseDirectory.filePath(baseName + QStringLiteral("_teletext")));
    }
    appendUniqueCandidate(candidateDirectories, baseDirectory.filePath(QStringLiteral("teletext_html")));
    appendUniqueCandidate(candidateDirectories, baseDirectory.filePath(QStringLiteral("teletext")));

    for (const QString &candidate : candidateDirectories) {
        if (!directoryContainsHtmlPages(candidate)) {
            continue;
        }
        return QDir(candidate).absolutePath();
    }

    return QString();
}

QString resolveTeletextDirectoryFromHints(const QStringList &pathHints)
{
    for (const QString &pathHint : pathHints) {
        const QString resolvedDirectory = resolveTeletextDirectoryFromHint(pathHint);
        if (!resolvedDirectory.isEmpty()) {
            return resolvedDirectory;
        }
    }
    return QString();
}

QStringList droppedLocalFiles(const QMimeData *mimeData)
{
    QStringList filePaths;
    if (!mimeData || !mimeData->hasUrls()) {
        return filePaths;
    }

    for (const QUrl &url : mimeData->urls()) {
        if (!url.isLocalFile()) {
            continue;
        }
        appendUniqueCandidate(filePaths, url.toLocalFile());
    }
    return filePaths;
}

QString qtTeletextCompatibilityStyle()
{
    return QStringLiteral(
        "<style id=\"qt-teletext-compat\">"
        "body { background: black !important; margin: 0 !important; padding: 0 !important; }"
        ".subpage {"
        "  float: none !important;"
        "  display: inline-block !important;"
        "  white-space: pre !important;"
        "  border: 0 !important;"
        "  text-shadow: none !important;"
        "  filter: none !important;"
        "  font-family: teletext2, \"Courier New\", monospace !important;"
        "  font-size: 20px !important;"
        "  line-height: 1.0 !important;"
        "  min-width: 40ch !important;"
        "  min-height: 25em !important;"
        "}"
        ".row { display: block !important; white-space: pre !important; }"
        ".dh {"
        "  font-family: teletext4, teletext2, \"Courier New\", monospace !important;"
        "  font-size: 200% !important;"
        "  line-height: 1.0 !important;"
        "}"
        "</style>");
}

QString normalizedTeletextHtmlForQt(QString htmlContent)
{
    if (htmlContent.contains(QStringLiteral("qt-teletext-compat"), Qt::CaseInsensitive)) {
        return htmlContent;
    }

    static const QRegularExpression rowBoundaryPattern(
        QStringLiteral("</span>\\s*<span\\s+class=\"row\">"),
        QRegularExpression::CaseInsensitiveOption
    );
    htmlContent.replace(rowBoundaryPattern, QStringLiteral("</span><br/><span class=\"row\">"));

    const QString styleBlock = qtTeletextCompatibilityStyle();
    const qint32 headCloseIndex = htmlContent.indexOf(QStringLiteral("</head>"), 0, Qt::CaseInsensitive);
    if (headCloseIndex >= 0) {
        htmlContent.insert(headCloseIndex, styleBlock);
    } else {
        htmlContent.prepend(QStringLiteral("<head>") + styleBlock + QStringLiteral("</head>"));
    }
    return htmlContent;
}
} // namespace

TeletextViewerDialog::TeletextViewerDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Teletext Viewer"));
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setMinimumSize(900, 650);
    setAcceptDrops(true);

    auto *mainLayout = new QVBoxLayout(this);

    auto *directoryLayout = new QHBoxLayout();
    directoryLayout->addWidget(new QLabel(tr("Directory:"), this));
    directoryLineEdit = new QLineEdit(this);
    directoryLineEdit->setReadOnly(true);
    directoryLayout->addWidget(directoryLineEdit, 1);
    browseDirectoryButton = new QPushButton(tr("Browse..."), this);
    refreshListButton = new QPushButton(tr("Refresh List"), this);
    directoryLayout->addWidget(browseDirectoryButton);
    directoryLayout->addWidget(refreshListButton);
    mainLayout->addLayout(directoryLayout);

    auto *pageLayout = new QHBoxLayout();
    pageLayout->addWidget(new QLabel(tr("Page:"), this));
    pageComboBox = new QComboBox(this);
    pageComboBox->setMinimumContentsLength(10);
    pageLayout->addWidget(pageComboBox, 1);
    refreshPageButton = new QPushButton(tr("Refresh Page"), this);
    openInBrowserButton = new QPushButton(tr("Open in Browser"), this);
    pageLayout->addWidget(refreshPageButton);
    pageLayout->addWidget(openInBrowserButton);
    mainLayout->addLayout(pageLayout);

    auto *optionsLayout = new QHBoxLayout();
    autoRefreshCheckBox = new QCheckBox(tr("Live refresh"), this);
    autoRefreshCheckBox->setChecked(true);
    optionsLayout->addWidget(autoRefreshCheckBox);
    optionsLayout->addStretch(1);
    mainLayout->addLayout(optionsLayout);

    pageViewer = new QTextBrowser(this);
    pageViewer->setOpenExternalLinks(false);
    pageViewer->setOpenLinks(false);
    mainLayout->addWidget(pageViewer, 1);

    refreshTimer = new QTimer(this);
    refreshTimer->setInterval(1000);
    refreshTimer->setSingleShot(false);

    connect(browseDirectoryButton, &QPushButton::clicked,
            this, &TeletextViewerDialog::browseForDirectory);
    connect(refreshListButton, &QPushButton::clicked,
            this, &TeletextViewerDialog::refreshPageList);
    connect(pageComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TeletextViewerDialog::loadSelectedPage);
    connect(refreshPageButton, &QPushButton::clicked,
            this, &TeletextViewerDialog::loadSelectedPage);
    connect(pageViewer, &QTextBrowser::anchorClicked,
            this, &TeletextViewerDialog::handlePageLinkClicked);
    connect(openInBrowserButton, &QPushButton::clicked,
            this, &TeletextViewerDialog::openSelectedPageInBrowser);
    connect(autoRefreshCheckBox, &QCheckBox::toggled,
            this, &TeletextViewerDialog::setAutoRefreshEnabled);
    connect(refreshTimer, &QTimer::timeout,
            this, &TeletextViewerDialog::handlePeriodicRefresh);
    const QList<QWidget *> dropTargets = {
        directoryLineEdit,
        pageComboBox,
        pageViewer
    };
    for (QWidget *dropTarget : dropTargets) {
        if (!dropTarget) {
            continue;
        }
        dropTarget->setAcceptDrops(true);
        dropTarget->installEventFilter(this);
    }
    if (pageViewer && pageViewer->viewport()) {
        pageViewer->viewport()->setAcceptDrops(true);
        pageViewer->viewport()->installEventFilter(this);
    }

    setAutoRefreshEnabled(autoRefreshCheckBox->isChecked());
}

void TeletextViewerDialog::setDirectory(const QString &directoryPath)
{
    const QString normalizedPath = QDir::cleanPath(directoryPath.trimmed());
    if (normalizedPath.isEmpty()) {
        return;
    }

    currentDirectoryPath = normalizedPath;
    directoryLineEdit->setText(currentDirectoryPath);
    refreshPageList();
}

QString TeletextViewerDialog::directory() const
{
    return currentDirectoryPath;
}
bool TeletextViewerDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (!event) {
        return QDialog::eventFilter(watched, event);
    }

    if (event->type() == QEvent::DragEnter) {
        auto *dragEnterEvent = static_cast<QDragEnterEvent *>(event);
        if (canAcceptDrop(dragEnterEvent->mimeData())) {
            dragEnterEvent->acceptProposedAction();
            return true;
        }
    } else if (event->type() == QEvent::DragMove) {
        auto *dragMoveEvent = static_cast<QDragMoveEvent *>(event);
        if (canAcceptDrop(dragMoveEvent->mimeData())) {
            dragMoveEvent->acceptProposedAction();
            return true;
        }
    } else if (event->type() == QEvent::Drop) {
        auto *dropEvent = static_cast<QDropEvent *>(event);
        if (handleDrop(dropEvent->mimeData())) {
            dropEvent->acceptProposedAction();
            return true;
        }
    }

    return QDialog::eventFilter(watched, event);
}

void TeletextViewerDialog::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event) {
        return;
    }
    if (canAcceptDrop(event->mimeData())) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void TeletextViewerDialog::dragMoveEvent(QDragMoveEvent *event)
{
    if (!event) {
        return;
    }
    if (canAcceptDrop(event->mimeData())) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void TeletextViewerDialog::dropEvent(QDropEvent *event)
{
    if (!event) {
        return;
    }
    if (handleDrop(event->mimeData())) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void TeletextViewerDialog::browseForDirectory()
{
    const QString startPath = currentDirectoryPath.isEmpty() ? QDir::homePath() : currentDirectoryPath;
    const QString selectedDirectory = QFileDialog::getExistingDirectory(
        this,
        tr("Select teletext HTML directory"),
        startPath,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );
    if (selectedDirectory.isEmpty()) {
        return;
    }

    setDirectory(selectedDirectory);
}

void TeletextViewerDialog::refreshPageList()
{
    pageComboBox->blockSignals(true);

    const QString previousSelection = pageComboBox->currentText();
    pageComboBox->clear();

    if (!directoryContainsHtml()) {
        pageComboBox->blockSignals(false);
        pageViewer->setHtml(tr("<html><body><p>No teletext HTML pages were found in this directory.</p></body></html>"));
        setWindowTitle(tr("Teletext Viewer"));
        lastLoadedPagePath.clear();
        lastLoadedPageModified = QDateTime();
        return;
    }

    const QDir directory(currentDirectoryPath);
    const QStringList htmlPages = directory.entryList(
        QStringList() << QStringLiteral("*.html"),
        QDir::Files,
        QDir::Name | QDir::IgnoreCase
    );
    pageComboBox->addItems(htmlPages);

    qint32 selectedIndex = htmlPages.indexOf(previousSelection);
    if (selectedIndex < 0) {
        selectedIndex = htmlPages.indexOf(QStringLiteral("100.html"));
    }
    if (selectedIndex < 0 && !htmlPages.isEmpty()) {
        selectedIndex = 0;
    }
    if (selectedIndex >= 0) {
        pageComboBox->setCurrentIndex(selectedIndex);
    }

    pageComboBox->blockSignals(false);
    loadSelectedPage();
}

void TeletextViewerDialog::loadSelectedPage()
{
    const QString pagePath = selectedPagePath();
    if (pagePath.isEmpty()) {
        return;
    }

    const QFileInfo pageInfo(pagePath);
    if (!pageInfo.exists()) {
        return;
    }

    if (lastLoadedPagePath == pagePath && lastLoadedPageModified == pageInfo.lastModified()) {
        return;
    }

    QFile pageFile(pagePath);
    if (!pageFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QString pageHtml = QString::fromUtf8(pageFile.readAll());
    pageHtml = normalizedTeletextHtmlForQt(pageHtml);
    pageViewer->setSearchPaths(QStringList() << currentDirectoryPath);
    pageViewer->document()->setBaseUrl(QUrl::fromLocalFile(pagePath));
    pageViewer->setHtml(pageHtml);

    lastLoadedPagePath = pagePath;
    lastLoadedPageModified = pageInfo.lastModified();
    setWindowTitle(tr("Teletext Viewer - %1").arg(pageInfo.fileName()));
}

void TeletextViewerDialog::handlePageLinkClicked(const QUrl &linkUrl)
{
    if (!linkUrl.isValid()) {
        return;
    }

    if (linkUrl.path().isEmpty() && !linkUrl.fragment().isEmpty()) {
        pageViewer->scrollToAnchor(linkUrl.fragment());
        return;
    }

    QUrl resolvedUrl = linkUrl;
    if (resolvedUrl.isRelative() && !lastLoadedPagePath.isEmpty()) {
        resolvedUrl = QUrl::fromLocalFile(lastLoadedPagePath).resolved(linkUrl);
    }

    if (resolvedUrl.isLocalFile()) {
        const QFileInfo linkedFileInfo(QDir::cleanPath(resolvedUrl.toLocalFile()));
        if (linkedFileInfo.exists() &&
            linkedFileInfo.isFile() &&
            linkedFileInfo.suffix().compare(QStringLiteral("html"), Qt::CaseInsensitive) == 0) {
            if (currentDirectoryPath.compare(linkedFileInfo.absolutePath(), Qt::CaseInsensitive) != 0) {
                setDirectory(linkedFileInfo.absolutePath());
            }

            const qint32 linkedPageIndex = pageComboBox->findText(
                linkedFileInfo.fileName(),
                Qt::MatchFixedString
            );
            if (linkedPageIndex >= 0) {
                if (pageComboBox->currentIndex() != linkedPageIndex) {
                    pageComboBox->setCurrentIndex(linkedPageIndex);
                } else {
                    loadSelectedPage();
                }
                return;
            }
        }
    }

    QDesktopServices::openUrl(resolvedUrl);
}

void TeletextViewerDialog::openSelectedPageInBrowser()
{
    const QString pagePath = selectedPagePath();
    if (pagePath.isEmpty()) {
        return;
    }

    const QUrl pageUrl = QUrl::fromLocalFile(pagePath);
    if (!QDesktopServices::openUrl(pageUrl)) {
        QMessageBox::warning(this, tr("Warning"),
                             tr("Could not open teletext page in browser:\n%1").arg(pagePath));
    }
}

void TeletextViewerDialog::setAutoRefreshEnabled(bool enabled)
{
    if (enabled) {
        refreshTimer->start();
    } else {
        refreshTimer->stop();
    }
}

void TeletextViewerDialog::handlePeriodicRefresh()
{
    refreshPageList();
}
bool TeletextViewerDialog::canAcceptDrop(const QMimeData *mimeData) const
{
    const QStringList filePaths = droppedLocalFiles(mimeData);
    if (filePaths.isEmpty()) {
        return false;
    }

    const QString resolvedDirectory = resolveTeletextDirectoryFromHints(filePaths);
    return !resolvedDirectory.isEmpty();
}

bool TeletextViewerDialog::handleDrop(const QMimeData *mimeData)
{
    const QStringList filePaths = droppedLocalFiles(mimeData);
    if (filePaths.isEmpty()) {
        return false;
    }

    const QString resolvedDirectory = resolveTeletextDirectoryFromHints(filePaths);
    if (resolvedDirectory.isEmpty()) {
        return false;
    }

    QString droppedHtmlPageName;
    for (const QString &filePath : filePaths) {
        const QFileInfo fileInfo(filePath);
        if (!fileInfo.exists() || !fileInfo.isFile()) {
            continue;
        }
        if (fileInfo.suffix().compare(QStringLiteral("html"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        if (fileInfo.absolutePath().compare(resolvedDirectory, Qt::CaseInsensitive) == 0) {
            droppedHtmlPageName = fileInfo.fileName();
            break;
        }
    }

    setDirectory(resolvedDirectory);
    if (!droppedHtmlPageName.isEmpty()) {
        const qint32 pageIndex = pageComboBox->findText(droppedHtmlPageName, Qt::MatchFixedString);
        if (pageIndex >= 0 && pageComboBox->currentIndex() != pageIndex) {
            pageComboBox->setCurrentIndex(pageIndex);
        } else if (pageIndex >= 0) {
            loadSelectedPage();
        }
    }
    return true;
}

bool TeletextViewerDialog::directoryContainsHtml() const
{
    return directoryContainsHtmlPages(currentDirectoryPath);
}

QString TeletextViewerDialog::selectedPagePath() const
{
    if (currentDirectoryPath.trimmed().isEmpty()) {
        return QString();
    }
    if (pageComboBox->currentIndex() < 0) {
        return QString();
    }

    return QDir(currentDirectoryPath).filePath(pageComboBox->currentText());
}
