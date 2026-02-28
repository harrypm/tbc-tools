#include "exportreadoutdialog.h"
#include "ui_exportreadoutdialog.h"
#include <QAbstractItemView>
#include <QHeaderView>
#include <QTableWidgetItem>

namespace {
void setTableItem(QTableWidget *table, int row, int column, const QString &text)
{
    if (!table) {
        return;
    }
    QTableWidgetItem *item = table->item(row, column);
    if (!item) {
        item = new QTableWidgetItem();
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, column, item);
    }
    item->setText(text);
}
} // namespace

ExportReadoutDialog::ExportReadoutDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ExportReadoutDialog)
{
    ui->setupUi(this);
    if (ui->exportReadoutProcessTable) {
        ui->exportReadoutProcessTable->setColumnCount(7);
        ui->exportReadoutProcessTable->setHorizontalHeaderLabels({
            tr("Process"),
            tr("TBC"),
            tr("Track"),
            tr("Current"),
            tr("Total"),
            tr("Errors"),
            tr("FPS")
        });
        ui->exportReadoutProcessTable->verticalHeader()->setVisible(false);
        ui->exportReadoutProcessTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        ui->exportReadoutProcessTable->setSelectionMode(QAbstractItemView::NoSelection);
        ui->exportReadoutProcessTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        ui->exportReadoutProcessTable->setAlternatingRowColors(true);
        ui->exportReadoutProcessTable->horizontalHeader()->setStretchLastSection(true);
        ui->exportReadoutProcessTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    }
}

ExportReadoutDialog::~ExportReadoutDialog()
{
    delete ui;
}

void ExportReadoutDialog::setDetails(const ExportReadoutDetails &details)
{
    ui->exportReadoutInputValueLabel->setText(details.inputFile);
    ui->exportReadoutOutputValueLabel->setText(details.outputBase);
    ui->exportReadoutProfileValueLabel->setText(details.profile);
    ui->exportReadoutAudioValueLabel->setText(details.audioTracks);
    ui->exportReadoutSystemValueLabel->setText(details.videoSystem);
    ui->exportReadoutChromaDecoderValueLabel->setText(details.chromaDecoder);
    ui->exportReadoutChromaGainValueLabel->setText(details.chromaGain);
    ui->exportReadoutChromaPhaseValueLabel->setText(details.chromaPhase);
    ui->exportReadoutLumaNrValueLabel->setText(details.lumaNr);
    ui->exportReadoutNtscAdaptiveValueLabel->setText(details.ntscAdaptive);
    ui->exportReadoutNtscAdaptThresholdValueLabel->setText(details.ntscAdaptThreshold);
    ui->exportReadoutNtscChromaWeightValueLabel->setText(details.ntscChromaWeight);
    ui->exportReadoutNtscPhaseCompValueLabel->setText(details.ntscPhaseComp);
    ui->exportReadoutPalTransformThresholdValueLabel->setText(details.palTransformThreshold);
    ui->exportReadoutActiveLinesValueLabel->setText(details.activeLines);
}

void ExportReadoutDialog::setStatus(const QString &status)
{
    ui->exportReadoutStatusValueLabel->setText(status);
}

void ExportReadoutDialog::setLastMessage(const QString &message)
{
    ui->exportReadoutLastMessageValueLabel->setText(message);
}

void ExportReadoutDialog::resetProcessStats()
{
    processRowMap.clear();
    if (ui->exportReadoutProcessTable) {
        ui->exportReadoutProcessTable->setRowCount(0);
    }
}

void ExportReadoutDialog::updateProcessStat(const ExportProcessStat &stat)
{
    if (!ui->exportReadoutProcessTable) {
        return;
    }
    const QString key = stat.process + QStringLiteral("|") + stat.tbcType;
    int row = processRowMap.value(key, -1);
    if (row < 0) {
        row = ui->exportReadoutProcessTable->rowCount();
        ui->exportReadoutProcessTable->insertRow(row);
        processRowMap.insert(key, row);
    }

    setTableItem(ui->exportReadoutProcessTable, row, 0, stat.process);
    setTableItem(ui->exportReadoutProcessTable, row, 1, stat.tbcType);
    setTableItem(ui->exportReadoutProcessTable, row, 2, stat.trackedName);
    setTableItem(ui->exportReadoutProcessTable, row, 3, stat.current);
    setTableItem(ui->exportReadoutProcessTable, row, 4, stat.total);
    setTableItem(ui->exportReadoutProcessTable, row, 5, stat.errors);
    setTableItem(ui->exportReadoutProcessTable, row, 6, stat.fps);
}
