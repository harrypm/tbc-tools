/******************************************************************************
 * metadataconversiondialog.cpp
 * tbc-metadata-converter - Metadata converter tool for ld-decode
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#include "metadataconversiondialog.h"
#include "ui_metadataconversiondialog.h"

#include "metadataconverterutil.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QMessageBox>
#include <QSignalBlocker>

MetadataConversionDialog::MetadataConversionDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MetadataConversionDialog)
{
    ui->setupUi(this);
    if (ui->directionValueLabel) {
        ui->directionValueLabel->setText(tr("Auto (select .json or .db input)"));
    }
    if (ui->inputLineEdit) {
        connect(ui->inputLineEdit, &QLineEdit::textChanged, this, [this]() {
            updateDirectionFromInput(false);
        });
    }
}

MetadataConversionDialog::~MetadataConversionDialog()
{
    delete ui;
}

void MetadataConversionDialog::setSourceDirectory(const QString &directory)
{
    if (!directory.isEmpty()) {
        sourceDirectory = directory;
    }
}

void MetadataConversionDialog::setDefaultInput(const QString &inputFilename)
{
    const QString normalizedInput = MetadataConverterUtil::normalizePathForCurrentPlatform(inputFilename);
    if (normalizedInput.isEmpty()) {
        return;
    }
    ui->inputLineEdit->setText(normalizedInput);
    updateDirectionFromInput(true);
    ui->statusLabel->clear();
    const QFileInfo inputInfo(normalizedInput);

    if (inputInfo.exists()) {
        sourceDirectory = inputInfo.absolutePath();
    }
}

void MetadataConversionDialog::on_inputBrowseButton_clicked()
{
    const QString filter = tr("Metadata input (*.json *.db);;JSON metadata (*.json);;SQLite metadata (*.db);;All Files (*)");
    const QString inputFileName = QFileDialog::getOpenFileName(this,
                                                               tr("Select metadata input"),
                                                               sourceDirectory,
                                                               filter);
    if (inputFileName.isEmpty()) {
        return;
    }

    const QString normalizedInput = MetadataConverterUtil::normalizePathForCurrentPlatform(inputFileName);
    ui->inputLineEdit->setText(normalizedInput);
    updateDirectionFromInput(true);
    ui->statusLabel->clear();

    const QFileInfo selectedInfo(normalizedInput);
    if (selectedInfo.exists()) {
        sourceDirectory = selectedInfo.absolutePath();
    }
}

void MetadataConversionDialog::on_outputBrowseButton_clicked()
{
    const QString normalizedInput = MetadataConverterUtil::normalizePathForCurrentPlatform(ui->inputLineEdit->text());
    const MetadataConverterUtil::MetadataConversionDirection direction =
        MetadataConverterUtil::inferMetadataConversionDirection(normalizedInput);
    const QString filter = (direction == MetadataConverterUtil::MetadataConversionDirection::JsonToSqlite)
                               ? tr("SQLite metadata (*.db);;All Files (*)")
                           : (direction == MetadataConverterUtil::MetadataConversionDirection::SqliteToJson)
                               ? tr("JSON metadata (*.json);;All Files (*)")
                               : tr("Metadata output (*.db *.json);;All Files (*)");
    QString suggestedOutput = ui->outputLineEdit->text().trimmed();
    if (suggestedOutput.isEmpty()) {
        suggestedOutput = MetadataConverterUtil::defaultMetadataOutputPath(normalizedInput, direction);
    }

    const QString outputFileName = QFileDialog::getSaveFileName(this,
                                                                tr("Select metadata output"),
                                                                suggestedOutput,
                                                                filter);
    if (outputFileName.isEmpty()) {
        return;
    }

    ui->outputLineEdit->setText(MetadataConverterUtil::normalizePathForCurrentPlatform(outputFileName));
    ui->statusLabel->clear();

    const QFileInfo selectedInfo(ui->outputLineEdit->text());
    if (!selectedInfo.absolutePath().isEmpty()) {
        sourceDirectory = selectedInfo.absolutePath();
    }
}


void MetadataConversionDialog::on_convertButton_clicked()
{
    const QString inputFileName = MetadataConverterUtil::normalizePathForCurrentPlatform(ui->inputLineEdit->text());
    const QString outputFileName = MetadataConverterUtil::normalizePathForCurrentPlatform(ui->outputLineEdit->text());
    if (inputFileName.isEmpty() || outputFileName.isEmpty()) {
        ui->statusLabel->setText(tr("Please choose both an input and output file."));
        return;
    }
    const MetadataConverterUtil::MetadataConversionDirection direction =
        MetadataConverterUtil::inferMetadataConversionDirection(inputFileName);
    if (direction == MetadataConverterUtil::MetadataConversionDirection::Unknown) {
        ui->statusLabel->setText(tr("Input must end with .json or .db so conversion direction can be determined."));
        return;
    }

    {
        const QSignalBlocker inputBlocker(ui->inputLineEdit);
        ui->inputLineEdit->setText(inputFileName);
    }
    {
        const QSignalBlocker outputBlocker(ui->outputLineEdit);
        ui->outputLineEdit->setText(outputFileName);
    }
    QString errorMessage;
    if (!MetadataConverterUtil::runMetadataConverter(direction, inputFileName, outputFileName, &errorMessage)) {
        ui->statusLabel->setText(tr("Conversion failed."));
        QMessageBox messageBox;
        messageBox.warning(this, tr("Error"), errorMessage);
        return;
    }

    ui->statusLabel->setText(tr("Conversion complete."));
}

void MetadataConversionDialog::updateDirectionFromInput(bool forceOutputUpdate)
{
    const QString normalizedInput = MetadataConverterUtil::normalizePathForCurrentPlatform(ui->inputLineEdit->text());
    if (!normalizedInput.isEmpty() && normalizedInput != ui->inputLineEdit->text()) {
        const QSignalBlocker blocker(ui->inputLineEdit);
        ui->inputLineEdit->setText(normalizedInput);
    }

    const MetadataConverterUtil::MetadataConversionDirection direction =
        MetadataConverterUtil::inferMetadataConversionDirection(normalizedInput);
    if (ui->directionValueLabel) {
        if (normalizedInput.isEmpty()) {
            ui->directionValueLabel->setText(tr("Auto (select .json or .db input)"));
        } else if (direction == MetadataConverterUtil::MetadataConversionDirection::JsonToSqlite) {
            ui->directionValueLabel->setText(tr("JSON → SQLite (auto)"));
        } else if (direction == MetadataConverterUtil::MetadataConversionDirection::SqliteToJson) {
            ui->directionValueLabel->setText(tr("SQLite → JSON (auto)"));
        } else {
            ui->directionValueLabel->setText(tr("Unknown (requires .json or .db input)"));
        }
    }
    const QString suggestedOutput = MetadataConverterUtil::defaultMetadataOutputPath(normalizedInput, direction);
    if ((forceOutputUpdate || ui->outputLineEdit->text().trimmed().isEmpty()) && !suggestedOutput.isEmpty()) {
        const QSignalBlocker blocker(ui->outputLineEdit);
        ui->outputLineEdit->setText(suggestedOutput);
    }

    if (normalizedInput.isEmpty() || direction != MetadataConverterUtil::MetadataConversionDirection::Unknown) {
        ui->statusLabel->clear();
    } else {
        ui->statusLabel->setText(tr("Input must end with .json or .db so conversion direction can be determined."));
    }
}
