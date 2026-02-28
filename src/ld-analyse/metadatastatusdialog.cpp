#include "metadatastatusdialog.h"
#include "ui_metadatastatusdialog.h"

MetadataStatusDialog::MetadataStatusDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MetadataStatusDialog)
{
    ui->setupUi(this);
}

MetadataStatusDialog::~MetadataStatusDialog()
{
    delete ui;
}

void MetadataStatusDialog::updateStatus(const MetadataStatusData &data)
{
    ui->metadataStatusDbValueLabel->setText(data.dbPath);
    ui->metadataStatusJsonValueLabel->setText(data.jsonPath);
    ui->metadataStatusSystemValueLabel->setText(data.videoSystem);
    ui->metadataStatusChromaDecoderValueLabel->setText(data.chromaDecoder);
    ui->metadataStatusChromaGainValueLabel->setText(data.chromaGain);
    ui->metadataStatusChromaPhaseValueLabel->setText(data.chromaPhase);
    ui->metadataStatusLumaNrValueLabel->setText(data.lumaNr);
    ui->metadataStatusNtscAdaptiveValueLabel->setText(data.ntscAdaptive);
    ui->metadataStatusNtscAdaptThresholdValueLabel->setText(data.ntscAdaptThreshold);
    ui->metadataStatusNtscChromaWeightValueLabel->setText(data.ntscChromaWeight);
    ui->metadataStatusNtscPhaseCompValueLabel->setText(data.ntscPhaseComp);
    ui->metadataStatusPalTransformThresholdValueLabel->setText(data.palTransformThreshold);
    ui->metadataStatusSavePendingValueLabel->setText(data.savePending);
}
