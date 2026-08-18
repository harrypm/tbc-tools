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
class QTextBrowser;
class QTimer;
class QUrl;

class TeletextViewerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TeletextViewerDialog(QWidget *parent = nullptr);
    void setDirectory(const QString &directoryPath);
    QString directory() const;
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void browseForDirectory();
    void refreshPageList();
    void loadSelectedPage();
    void handlePageLinkClicked(const QUrl &linkUrl);
    void openSelectedPageInBrowser();
    void setAutoRefreshEnabled(bool enabled);
    void handlePeriodicRefresh();

private:
    bool canAcceptDrop(const QMimeData *mimeData) const;
    bool handleDrop(const QMimeData *mimeData);
    bool directoryContainsHtml() const;
    QString selectedPagePath() const;

    QString currentDirectoryPath;
    QString lastLoadedPagePath;
    QDateTime lastLoadedPageModified;

    QLineEdit *directoryLineEdit = nullptr;
    QPushButton *browseDirectoryButton = nullptr;
    QPushButton *refreshListButton = nullptr;
    QComboBox *pageComboBox = nullptr;
    QPushButton *refreshPageButton = nullptr;
    QPushButton *openInBrowserButton = nullptr;
    QCheckBox *autoRefreshCheckBox = nullptr;
    QTextBrowser *pageViewer = nullptr;
    QTimer *refreshTimer = nullptr;
};

#endif // TELETEXTVIEWERDIALOG_H
