/******************************************************************************
 * teletextviewerdialog.h
 * tbc-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018-2026 Simon Inns
 * SPDX-FileCopyrightText: 2026 Harry Munday
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#ifndef TELETEXTVIEWERDIALOG_H
#define TELETEXTVIEWERDIALOG_H

#include <QDateTime>
#include <QDialog>

class QCheckBox;
class QComboBox;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QEvent;
class QLineEdit;
class QMimeData;
class QPushButton;
class QStackedWidget;
class QShowEvent;
class QTextBrowser;
class QTimer;
class QUrl;
class TeletextNativeViewWidget;

class TeletextViewerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TeletextViewerDialog(QWidget *parent = nullptr);
    void setDirectory(const QString &directoryPath);
    QString directory() const;
    bool openTeletextStream(const QString &streamPath, QString *errorMessage = nullptr);
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void browseForDirectory();
    void browseForTeletextStream();
    void refreshPageList();
    void loadSelectedPage();
    void handlePageLinkClicked(const QUrl &linkUrl);
    void openSelectedPageInBrowser();
    void setAutoRefreshEnabled(bool enabled);
    void handlePeriodicRefresh();
    void setNativeRendererEnabled(bool enabled);
    void setFlashAnimationEnabled(bool enabled);

private:
    void autoSizeWindowForCurrentRenderer();
    bool cyclePageSelection(int direction);
    bool openTeletextStreamPath(const QString &streamPath, QString *errorMessage = nullptr);
    bool canAcceptDrop(const QMimeData *mimeData) const;
    bool handleDrop(const QMimeData *mimeData);
    bool directoryContainsHtml() const;
    QString selectedPagePath() const;

    QString currentDirectoryPath;
    QString lastLoadedPagePath;
    QDateTime lastLoadedPageModified;

    QLineEdit *directoryLineEdit = nullptr;
    QPushButton *browseDirectoryButton = nullptr;
    QPushButton *browseStreamButton = nullptr;
    QPushButton *refreshListButton = nullptr;
    QComboBox *pageComboBox = nullptr;
    QPushButton *refreshPageButton = nullptr;
    QPushButton *openInBrowserButton = nullptr;
    QCheckBox *autoRefreshCheckBox = nullptr;
    QComboBox *rendererComboBox = nullptr;
    QCheckBox *flashAnimationCheckBox = nullptr;
    QStackedWidget *viewerStack = nullptr;
    QTextBrowser *pageViewer = nullptr;
    TeletextNativeViewWidget *nativePageViewer = nullptr;
    QTimer *refreshTimer = nullptr;
    bool autoWindowSizePending = true;
};

#endif // TELETEXTVIEWERDIALOG_H
