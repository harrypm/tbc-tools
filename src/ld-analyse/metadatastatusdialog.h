#ifndef METADATASTATUSDIALOG_H
#define METADATASTATUSDIALOG_H

#include <QDialog>
#include <QString>

namespace Ui {
class MetadataStatusDialog;
}

struct MetadataStatusData {
    QString dbPath;
    QString jsonPath;
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
    QString savePending;
};

class MetadataStatusDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MetadataStatusDialog(QWidget *parent = nullptr);
    ~MetadataStatusDialog();

    void updateStatus(const MetadataStatusData &data);

private:
    Ui::MetadataStatusDialog *ui;
};

#endif // METADATASTATUSDIALOG_H
