/************************************************************************

    e2eexportdriver.h

    Headless end-to-end driver for the tbc-analyse Export tab.

    Drives the real MainWindow + ExportDialog exactly as a user would:
    waits for the source to load, sets Export-tab widgets by objectName
    (refusing widgets the user could not touch because they are disabled),
    clicks Export, auto-answers modal message boxes while recording them,
    waits for the run to finish and writes a JSON result. Used by
    scripts/test-export-e2e.py (run with QT_QPA_PLATFORM=offscreen) against
    build trees and unpacked release packages alike.

    This file is part of tbc-tools.

    tbc-tools is free software: you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

************************************************************************/

#ifndef E2EEXPORTDRIVER_H
#define E2EEXPORTDRIVER_H

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>

class ExportDialog;
class QWidget;

class E2EExportDriver : public QObject
{
    Q_OBJECT

public:
    enum class Mode {
        Export,      // apply the spec and run an export
        ListOptions  // dump every Export-tab control and its choices
    };

    // specOrOutputPath: the spec JSON (Export) or the output JSON (ListOptions)
    E2EExportDriver(QWidget *mainWindow, Mode mode, const QString &specOrOutputPath,
                    QObject *parent = nullptr);

    void start();

private:
    enum class State { WaitingForSource, Running, Done };

    void tick();
    void handleModal();
    bool applySpec(QString *errorMessage);
    QJsonObject describeControls() const;
    void finish(bool ok, const QString &reason);

    QWidget *mainWindow;
    Mode mode;
    QString path;
    QJsonObject spec;
    QPointer<ExportDialog> dialog;
    QTimer timer;
    QElapsedTimer clock;
    State state = State::WaitingForSource;
    bool clickInProgress = false;
    qint64 timeoutMs = 900000;
    QJsonArray modals;
    // The dialog clears its log panes when a run ends, so lines are
    // captured as they are appended rather than read back afterwards.
    QStringList capturedLog;
    QStringList capturedProxyLog;
    int modalErrors = 0;
};

#endif // E2EEXPORTDRIVER_H
