#ifndef EXPORTDIALOG_H
#define EXPORTDIALOG_H

#include <QWidget>
#include <QProcess>
#include <QHash>

class TbcSource;

namespace Ui {
class ExportDialog;
}

class ExportDialog : public QWidget
{
    Q_OBJECT

public:
    struct ExportProcessStat {
        QString process;
        QString tbcType;
        QString trackedName;
        QString current;
        QString total;
        QString errors;
        QString fps;
    };
    explicit ExportDialog(QWidget *parent = nullptr);
    ~ExportDialog();

    void setSource(TbcSource *source);

private slots:
    void on_outputBrowseButton_clicked();
    void on_audio1BrowseButton_clicked();
    void on_audio2BrowseButton_clicked();
    void on_audio3BrowseButton_clicked();
    void on_audio4BrowseButton_clicked();
    void on_exportButton_clicked();
    void on_cancelButton_clicked();

    void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleProcessError(QProcess::ProcessError error);
    void handleProcessStdout();
    void handleProcessStderr();

private:
    void updateFromSource();
    void refreshProfiles();
    void setBusy(bool busy);
    QString resolveVideoExportPath() const;
    QString defaultOutputBaseName(const QString &inputFile) const;
    QString sanitizeOutputBaseName(const QString &path) const;
    QString videoSystemArg(int system) const;
    QStringList collectAudioTracks() const;
    QStringList buildArguments(QString *errorMessage) const;
    void appendStatus(const QString &message);
    void appendLog(const QString &message);
    QString formatCommand(const QString &program, const QStringList &args) const;
    void resetProcessStats();
    void updateProcessStat(const ExportProcessStat &stat);

    Ui::ExportDialog *ui;
    TbcSource *tbcSource = nullptr;
    QProcess *exportProcess = nullptr;
    QString currentInputFile;
    QString processStdout;
    QString processStderr;
    bool outputAutoSet = true;
    bool exportAvailable = false;
    QHash<QString, int> processRowMap;
};

#endif // EXPORTDIALOG_H
