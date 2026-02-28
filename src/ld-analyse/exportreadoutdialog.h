#ifndef EXPORTREADOUTDIALOG_H
#define EXPORTREADOUTDIALOG_H

#include <QDialog>
#include <QHash>
#include <QString>

namespace Ui {
class ExportReadoutDialog;
}

class ExportReadoutDialog : public QDialog
{
    Q_OBJECT

public:
    struct ExportReadoutDetails {
        QString inputFile;
        QString outputBase;
        QString profile;
        QString audioTracks;
        QString videoSystem;
        QString chromaDecoder;
        QString chromaGain;
        QString chromaPhase;
        QString lumaNr;
        QString ntscAdaptive;
        QString ntscAdaptThreshold;
        QString ntscChromaWeight;
        QString ntscPhaseComp;
        QString palTransformThreshold;
        QString activeLines;
    };
    struct ExportProcessStat {
        QString process;
        QString tbcType;
        QString trackedName;
        QString current;
        QString total;
        QString errors;
        QString fps;
    };
    explicit ExportReadoutDialog(QWidget *parent = nullptr);
    ~ExportReadoutDialog();
    void setDetails(const ExportReadoutDetails &details);
    void setStatus(const QString &status);
    void setLastMessage(const QString &message);
    void resetProcessStats();
    void updateProcessStat(const ExportProcessStat &stat);

private:
    Ui::ExportReadoutDialog *ui;
    QHash<QString, int> processRowMap;
};

#endif // EXPORTREADOUTDIALOG_H
