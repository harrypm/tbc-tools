/******************************************************************************
 * vbiprocessingdialog.cpp
 * tbc-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Harry Munday
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#include "vbiprocessingdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

VbiProcessingDialog::VbiProcessingDialog(const VbiProcessingOptions &current, QWidget *parent)
    : QDialog(parent), currentOptions(current)
{
    setWindowTitle(tr("Process VBI"));
    setMinimumWidth(420);

    auto *mainLayout = new QVBoxLayout(this);

    auto *introLabel = new QLabel(tr("Select which VBI/VITS data types to process:"), this);
    mainLayout->addWidget(introLabel);

    // Six checkboxes. The four in-process VBI decoders default on;
    // teletext and VITS are opt-in (off by default).
    vbiCoreCheckBox = new QCheckBox(tr("VBI core (frame number / timecode / chapter / user code)"), this);
    vbiCoreCheckBox->setToolTip(tr("Biphase-coded VBI on field lines 16-18 (PAL) / equivalent NTSC lines."));
    vbiCoreCheckBox->setChecked(currentOptions.vbiCore);
    mainLayout->addWidget(vbiCoreCheckBox);

    ntscCheckBox = new QCheckBox(tr("NTSC-specific (FM code / white flag / video ID)"), this);
    ntscCheckBox->setToolTip(tr("NTSC LaserDisc-specific data on field lines 10/11/20. Only applies to NTSC sources."));
    ntscCheckBox->setChecked(currentOptions.ntsc);
    mainLayout->addWidget(ntscCheckBox);

    vitcCheckBox = new QCheckBox(tr("VITC (vertical interval timecode)"), this);
    vitcCheckBox->setToolTip(tr("SMPTE VITC timecode scanned across its candidate field lines."));
    vitcCheckBox->setChecked(currentOptions.vitc);
    mainLayout->addWidget(vitcCheckBox);

    closedCaptionsCheckBox = new QCheckBox(tr("Closed captions (CEA-608)"), this);
    closedCaptionsCheckBox->setToolTip(tr("Line 21 (525-line) / line 22 (625-line) closed caption data."));
    closedCaptionsCheckBox->setChecked(currentOptions.closedCaptions);
    mainLayout->addWidget(closedCaptionsCheckBox);

    teletextCheckBox = new QCheckBox(tr("Teletext (HTML export)"), this);
    teletextCheckBox->setToolTip(tr("Decode teletext and write generated HTML pages. Off by default - most tapes have no teletext. Failure is non-fatal (VBI metadata is preserved)."));
    teletextCheckBox->setChecked(currentOptions.teletext);
    mainLayout->addWidget(teletextCheckBox);

    vitsCheckBox = new QCheckBox(tr("VITS metrics (SNR)"), this);
    vitsCheckBox->setToolTip(tr("Run the VITS analyser in-process to (re)compute white/black SNR metrics. Off by default - usually only needed after dropout correction / stacking changes the video. Failure is non-fatal."));
    vitsCheckBox->setChecked(currentOptions.vits);
    mainLayout->addWidget(vitsCheckBox);

    // Teletext advanced options - only enabled when teletext is ticked.
    teletextGroupBox = new QGroupBox(tr("Teletext options"), this);
    auto *teletextForm = new QFormLayout(teletextGroupBox);

    auto *htmlDirRow = new QHBoxLayout();
    teletextHtmlDirLineEdit = new QLineEdit(teletextGroupBox);
    teletextHtmlDirLineEdit->setPlaceholderText(tr("Default: <input>_teletext_html beside the TBC"));
    teletextHtmlDirLineEdit->setText(currentOptions.teletextHtmlDir);
    auto *browseButton = new QPushButton(tr("Browse..."), teletextGroupBox);
    htmlDirRow->addWidget(teletextHtmlDirLineEdit);
    htmlDirRow->addWidget(browseButton);
    teletextForm->addRow(tr("HTML output directory:"), htmlDirRow);

    teletextTapeFormatLineEdit = new QLineEdit(teletextGroupBox);
    teletextTapeFormatLineEdit->setText(currentOptions.teletextTapeFormat.isEmpty()
                                        ? QStringLiteral("vhs")
                                        : currentOptions.teletextTapeFormat);
    teletextForm->addRow(tr("Tape format profile:"), teletextTapeFormatLineEdit);

    teletextMinDuplicatesSpinBox = new QSpinBox(teletextGroupBox);
    teletextMinDuplicatesSpinBox->setRange(1, 999);
    teletextMinDuplicatesSpinBox->setValue(currentOptions.teletextMinDuplicates < 1
                                           ? 1
                                           : currentOptions.teletextMinDuplicates);
    teletextForm->addRow(tr("Minimum duplicates (squash):"), teletextMinDuplicatesSpinBox);

    mainLayout->addWidget(teletextGroupBox);

    // OK / Cancel
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(teletextCheckBox, &QCheckBox::toggled, this, &VbiProcessingDialog::onTeletextToggled);
    connect(browseButton, &QPushButton::clicked, this, &VbiProcessingDialog::onBrowseTeletextDir);

    // Apply the initial enabled state of the teletext options group.
    onTeletextToggled(teletextCheckBox->isChecked());
}

VbiProcessingDialog::~VbiProcessingDialog() = default;

void VbiProcessingDialog::onTeletextToggled(bool checked)
{
    teletextGroupBox->setEnabled(checked);
}

void VbiProcessingDialog::onBrowseTeletextDir()
{
    const QString startDir = teletextHtmlDirLineEdit->text().trimmed();
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select teletext HTML output directory"),
        startDir.isEmpty() ? QString() : startDir);
    if (!dir.isEmpty()) {
        teletextHtmlDirLineEdit->setText(dir);
    }
}

VbiProcessingOptions VbiProcessingDialog::selectedOptions() const
{
    VbiProcessingOptions result;
    result.vbiCore = vbiCoreCheckBox->isChecked();
    result.ntsc = ntscCheckBox->isChecked();
    result.vitc = vitcCheckBox->isChecked();
    result.closedCaptions = closedCaptionsCheckBox->isChecked();
    result.teletext = teletextCheckBox->isChecked();
    result.vits = vitsCheckBox->isChecked();
    result.teletextHtmlDir = teletextHtmlDirLineEdit->text().trimmed();
    result.teletextTapeFormat = teletextTapeFormatLineEdit->text().trimmed();
    result.teletextMinDuplicates = teletextMinDuplicatesSpinBox->value();
    return result;
}
