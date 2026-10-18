/******************************************************************************
 * advancedexportoptionsdialog.cpp
 * tbc-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Harry Munday
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#include "advancedexportoptionsdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

AdvancedExportOptionsDialog::AdvancedExportOptionsDialog(const ExportAdvancedOptions &current,
                                                         bool letterboxCropAllowed,
                                                         QWidget *parent)
    : QDialog(parent), currentOptions(current), letterboxCropAllowed(letterboxCropAllowed)
{
    setWindowTitle(tr("Advanced Export Options"));
    setMinimumWidth(460);

    auto *mainLayout = new QVBoxLayout(this);

    auto *introLabel = new QLabel(tr("Advanced options for the tbc-video-export pipeline "
                                     "(applied to both the main and the proxy exports):"), this);
    introLabel->setWordWrap(true);
    mainLayout->addWidget(introLabel);

    exportMetadataCheckBox = new QCheckBox(tr("Include FFmpeg metadata + CC"), this);
    exportMetadataCheckBox->setToolTip(
        tr("Include FFmpeg-compatible metadata and closed captions from tbc-export-metadata."));
    exportMetadataCheckBox->setChecked(currentOptions.exportMetadata);
    mainLayout->addWidget(exportMetadataCheckBox);

    disableMetadataEmbeddingCheckBox = new QCheckBox(tr("Disable decode JSON attachment"), this);
    disableMetadataEmbeddingCheckBox->setToolTip(
        tr("Disable embedding the decode .tbc.json file as an attachment in supported containers."));
    disableMetadataEmbeddingCheckBox->setChecked(currentOptions.disableMetadataEmbedding);
    mainLayout->addWidget(disableMetadataEmbeddingCheckBox);

    letterboxCropCheckBox = new QCheckBox(tr("Letterbox crop"), this);
    letterboxCropCheckBox->setToolTip(
        letterboxCropAllowed
            ? tr("Adjust active lines for letterbox crop output. Only available with default "
                 "Active Area framing; cannot be combined with force anamorphic.")
            : tr("Adjust active lines for letterbox crop output. Only available with default "
                 "Active Area framing (not currently selected on the Export page)."));
    letterboxCropCheckBox->setChecked(currentOptions.letterboxCrop && letterboxCropAllowed);
    letterboxCropCheckBox->setEnabled(letterboxCropAllowed);
    mainLayout->addWidget(letterboxCropCheckBox);

    forceAnamorphicCheckBox = new QCheckBox(tr("Force anamorphic"), this);
    forceAnamorphicCheckBox->setToolTip(
        tr("Force widescreen display aspect ratio metadata. Cannot be combined with letterbox crop."));
    forceAnamorphicCheckBox->setChecked(currentOptions.forceAnamorphic);
    mainLayout->addWidget(forceAnamorphicCheckBox);

    reverseFieldOrderCheckBox = new QCheckBox(tr("Reverse field order (second/first)"), this);
    reverseFieldOrderCheckBox->setToolTip(
        tr("Reverse the field order to second/first, for sources captured with a reversed field "
           "order. Dropout correction is not supported with reversed field order, so it is "
           "disabled automatically."));
    reverseFieldOrderCheckBox->setChecked(currentOptions.reverseFieldOrder);
    mainLayout->addWidget(reverseFieldOrderCheckBox);

    reverseNoteLabel = new QLabel(
        tr("Note: dropout correction is disabled automatically while field order is reversed."), this);
    reverseNoteLabel->setWordWrap(true);
    reverseNoteLabel->setVisible(reverseFieldOrderCheckBox->isChecked());
    mainLayout->addWidget(reverseNoteLabel);

    twoStepCheckBox = new QCheckBox(tr("Two-step merge (files instead of pipes)"), this);
    twoStepCheckBox->setToolTip(
        tr("Export Luma to an intermediate file before merging with Chroma. Use this as a "
           "fallback if named pipes cause issues on your system; it is slower and uses more "
           "disk space."));
    twoStepCheckBox->setChecked(currentOptions.twoStep);
    mainLayout->addWidget(twoStepCheckBox);

    auto *tuningGroupBox = new QGroupBox(tr("Pipeline tuning"), this);
    auto *tuningForm = new QFormLayout(tuningGroupBox);

    outputPaddingSpinBox = new QSpinBox(tuningGroupBox);
    outputPaddingSpinBox->setRange(0, 64);
    outputPaddingSpinBox->setSpecialValueText(tr("Off"));
    outputPaddingSpinBox->setValue(currentOptions.outputPadding);
    tuningForm->addRow(tr("Output padding (pixels):"), outputPaddingSpinBox);

    threadsSpinBox = new QSpinBox(tuningGroupBox);
    threadsSpinBox->setRange(0, 256);
    threadsSpinBox->setSpecialValueText(tr("Auto"));
    threadsSpinBox->setValue(currentOptions.threads);
    tuningForm->addRow(tr("Threads per process:"), threadsSpinBox);

    mainLayout->addWidget(tuningGroupBox);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(letterboxCropCheckBox, &QCheckBox::toggled,
            this, &AdvancedExportOptionsDialog::onLetterboxToggled);
    connect(forceAnamorphicCheckBox, &QCheckBox::toggled,
            this, &AdvancedExportOptionsDialog::onForceAnamorphicToggled);
    connect(reverseFieldOrderCheckBox, &QCheckBox::toggled,
            this, &AdvancedExportOptionsDialog::onReverseFieldOrderToggled);

    // Apply the initial mutual-exclusion state (letterbox crop and force
    // anamorphic cannot be combined).
    if (letterboxCropCheckBox->isChecked() && forceAnamorphicCheckBox->isChecked()) {
        const QSignalBlocker blocker(forceAnamorphicCheckBox);
        forceAnamorphicCheckBox->setChecked(false);
    }
}

AdvancedExportOptionsDialog::~AdvancedExportOptionsDialog() = default;

void AdvancedExportOptionsDialog::onLetterboxToggled(bool checked)
{
    if (checked && forceAnamorphicCheckBox->isChecked()) {
        const QSignalBlocker blocker(forceAnamorphicCheckBox);
        forceAnamorphicCheckBox->setChecked(false);
    }
}

void AdvancedExportOptionsDialog::onForceAnamorphicToggled(bool checked)
{
    if (checked && letterboxCropCheckBox->isChecked()) {
        const QSignalBlocker blocker(letterboxCropCheckBox);
        letterboxCropCheckBox->setChecked(false);
    }
}

void AdvancedExportOptionsDialog::onReverseFieldOrderToggled(bool checked)
{
    reverseNoteLabel->setVisible(checked);
}

ExportAdvancedOptions AdvancedExportOptionsDialog::selectedOptions() const
{
    ExportAdvancedOptions result;
    result.exportMetadata = exportMetadataCheckBox->isChecked();
    result.disableMetadataEmbedding = disableMetadataEmbeddingCheckBox->isChecked();
    result.letterboxCrop = letterboxCropCheckBox->isChecked();
    result.forceAnamorphic = forceAnamorphicCheckBox->isChecked();
    result.reverseFieldOrder = reverseFieldOrderCheckBox->isChecked();
    result.twoStep = twoStepCheckBox->isChecked();
    result.outputPadding = outputPaddingSpinBox->value();
    result.threads = threadsSpinBox->value();
    return result;
}
