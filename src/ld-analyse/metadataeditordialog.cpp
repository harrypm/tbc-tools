/******************************************************************************
 * metadataeditordialog.cpp
 * ld-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Harry Munday
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#include "metadataeditordialog.h"
#include "ui_metadataeditordialog.h"

#include <QSignalBlocker>

MetadataEditorDialog::MetadataEditorDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MetadataEditorDialog)
{
    ui->setupUi(this);
    setWindowFlags(Qt::Window);

    // Populate the system combo box (order must match VideoSystem enum)
    ui->systemComboBox->addItem(tr("PAL"), static_cast<int>(PAL));
    ui->systemComboBox->addItem(tr("NTSC"), static_cast<int>(NTSC));
    ui->systemComboBox->addItem(tr("PAL-M"), static_cast<int>(PAL_M));
    ui->systemComboBox->addItem(tr("SECAM"), static_cast<int>(SECAM));
    ui->systemComboBox->addItem(tr("MESECAM"), static_cast<int>(MESECAM));

    // Spinbox defaults for -1 = auto sentinel
    ui->chromaGainSpinBox->setValue(-1.0);
    ui->chromaPhaseSpinBox->setValue(-1.0);
    ui->lumaNrSpinBox->setValue(-1.0);
    ui->ntscAdaptThresholdSpinBox->setValue(-1.0);
    ui->ntscChromaWeightSpinBox->setValue(-1.0);
    ui->palTransformThresholdSpinBox->setValue(-1.0);

    // Connect signals
    connect(ui->systemComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MetadataEditorDialog::onSystemChanged);
    connect(ui->chromaDecoderComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MetadataEditorDialog::onChromaDecoderChanged);
    connect(ui->widescreenCheckBox, &QCheckBox::toggled,
            this, &MetadataEditorDialog::onWidescreenToggled);
    connect(ui->subcarrierLockedCheckBox, &QCheckBox::toggled,
            this, &MetadataEditorDialog::onSubcarrierLockedToggled);
    connect(ui->mappedCheckBox, &QCheckBox::toggled,
            this, &MetadataEditorDialog::onMappedToggled);
    connect(ui->tapeFormatLineEdit, &QLineEdit::textChanged,
            this, &MetadataEditorDialog::onTapeFormatChanged);
    connect(ui->chromaGainSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &MetadataEditorDialog::onChromaGainChanged);
    connect(ui->chromaPhaseSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &MetadataEditorDialog::onChromaPhaseChanged);
    connect(ui->lumaNrSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &MetadataEditorDialog::onLumaNrChanged);
    connect(ui->ntscAdaptiveCheckBox, &QCheckBox::toggled,
            this, &MetadataEditorDialog::onNtscAdaptiveToggled);
    connect(ui->ntscAdaptThresholdSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &MetadataEditorDialog::onNtscAdaptThresholdChanged);
    connect(ui->ntscChromaWeightSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &MetadataEditorDialog::onNtscChromaWeightChanged);
    connect(ui->ntscPhaseCompensationCheckBox, &QCheckBox::toggled,
            this, &MetadataEditorDialog::onNtscPhaseCompensationToggled);
    connect(ui->palTransformThresholdSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &MetadataEditorDialog::onPalTransformThresholdChanged);
    connect(ui->sampleRateSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &MetadataEditorDialog::onSampleRateChanged);
    connect(ui->colourBurstStartSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MetadataEditorDialog::onColourBurstStartChanged);
    connect(ui->colourBurstEndSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MetadataEditorDialog::onColourBurstEndChanged);
    connect(ui->gitBranchLineEdit, &QLineEdit::textChanged,
            this, &MetadataEditorDialog::onGitBranchChanged);
    connect(ui->gitCommitLineEdit, &QLineEdit::textChanged,
            this, &MetadataEditorDialog::onGitCommitChanged);
    connect(ui->pcmSampleRateSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &MetadataEditorDialog::onPcmSampleRateChanged);
    connect(ui->pcmBitsSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MetadataEditorDialog::onPcmBitsChanged);
    connect(ui->pcmSignedCheckBox, &QCheckBox::toggled,
            this, &MetadataEditorDialog::onPcmSignedToggled);
    connect(ui->pcmLittleEndianCheckBox, &QCheckBox::toggled,
            this, &MetadataEditorDialog::onPcmLittleEndianToggled);

    // Button box
    connect(ui->buttonBox, &QDialogButtonBox::clicked, this, [this](QAbstractButton *button) {
        const auto role = ui->buttonBox->buttonRole(button);
        if (role == QDialogButtonBox::ApplyRole) {
            onApplyClicked();
        } else if (role == QDialogButtonBox::AcceptRole) {
            onOkClicked();
        } else if (role == QDialogButtonBox::RejectRole) {
            onCancelClicked();
        }
    });
}

MetadataEditorDialog::~MetadataEditorDialog()
{
    delete ui;
}

void MetadataEditorDialog::populateChromaDecoderCombo(VideoSystem system)
{
    QSignalBlocker blocker(ui->chromaDecoderComboBox);
    ui->chromaDecoderComboBox->clear();
    if (system == PAL || system == PAL_M || system == SECAM || system == MESECAM) {
        ui->chromaDecoderComboBox->addItem(tr("mono"), QStringLiteral("mono"));
        ui->chromaDecoderComboBox->addItem(tr("pal2d"), QStringLiteral("pal2d"));
        ui->chromaDecoderComboBox->addItem(tr("transform2d"), QStringLiteral("transform2d"));
        ui->chromaDecoderComboBox->addItem(tr("transform3d"), QStringLiteral("transform3d"));
        ui->chromaDecoderComboBox->addItem(tr("secam"), QStringLiteral("secam"));
    } else {
        // NTSC
        ui->chromaDecoderComboBox->addItem(tr("mono"), QStringLiteral("mono"));
        ui->chromaDecoderComboBox->addItem(tr("ntsc1d"), QStringLiteral("ntsc1d"));
        ui->chromaDecoderComboBox->addItem(tr("ntsc2d"), QStringLiteral("ntsc2d"));
        ui->chromaDecoderComboBox->addItem(tr("ntsc3d"), QStringLiteral("ntsc3d"));
        ui->chromaDecoderComboBox->addItem(tr("nntransform3d"), QStringLiteral("nntransform3d"));
    }
}

void MetadataEditorDialog::setVideoParameters(const TbcMetaData::VideoParameters &videoParameters)
{
    m_videoParameters = videoParameters;
    m_populating = true;

    // System
    const int systemIndex = ui->systemComboBox->findData(static_cast<int>(videoParameters.system));
    if (systemIndex >= 0) ui->systemComboBox->setCurrentIndex(systemIndex);
    populateChromaDecoderCombo(videoParameters.system);

    // Chroma decoder
    const int decoderIndex = ui->chromaDecoderComboBox->findData(videoParameters.chromaDecoder.toLower());
    if (decoderIndex >= 0) {
        ui->chromaDecoderComboBox->setCurrentIndex(decoderIndex);
    } else if (!videoParameters.chromaDecoder.isEmpty()) {
        // Unknown decoder string — add it as a custom entry
        ui->chromaDecoderComboBox->addItem(videoParameters.chromaDecoder, videoParameters.chromaDecoder.toLower());
        ui->chromaDecoderComboBox->setCurrentIndex(ui->chromaDecoderComboBox->count() - 1);
    }

    // Checkboxes
    ui->widescreenCheckBox->setChecked(videoParameters.isWidescreen);
    ui->subcarrierLockedCheckBox->setChecked(videoParameters.isSubcarrierLocked);
    ui->mappedCheckBox->setChecked(videoParameters.isMapped);
    ui->ntscAdaptiveCheckBox->setChecked(videoParameters.ntscAdaptive == 1);
    ui->ntscPhaseCompensationCheckBox->setChecked(videoParameters.ntscPhaseCompensation == 1);

    // Line edits
    ui->tapeFormatLineEdit->setText(videoParameters.tapeFormat);
    ui->gitBranchLineEdit->setText(videoParameters.gitBranch);
    ui->gitCommitLineEdit->setText(videoParameters.gitCommit);

    // Double spinboxes (-1 = auto sentinel)
    ui->chromaGainSpinBox->setValue(videoParameters.chromaGain);
    ui->chromaPhaseSpinBox->setValue(videoParameters.chromaPhase);
    ui->lumaNrSpinBox->setValue(videoParameters.lumaNR);
    ui->ntscAdaptThresholdSpinBox->setValue(videoParameters.ntscAdaptThreshold);
    ui->ntscChromaWeightSpinBox->setValue(videoParameters.ntscChromaWeight);
    ui->palTransformThresholdSpinBox->setValue(videoParameters.palTransformThreshold);
    ui->sampleRateSpinBox->setValue(videoParameters.sampleRate);

    // Integer spinboxes
    ui->colourBurstStartSpinBox->setValue(videoParameters.colourBurstStart);
    ui->colourBurstEndSpinBox->setValue(videoParameters.colourBurstEnd);
    m_populating = false;
}

void MetadataEditorDialog::setPcmAudioParameters(const TbcMetaData::PcmAudioParameters &pcmAudioParameters)
{
    m_pcmAudioParameters = pcmAudioParameters;
    m_populating = true;
    ui->pcmSampleRateSpinBox->setValue(pcmAudioParameters.sampleRate);
    ui->pcmBitsSpinBox->setValue(pcmAudioParameters.bits);
    ui->pcmSignedCheckBox->setChecked(pcmAudioParameters.isSigned);
    ui->pcmLittleEndianCheckBox->setChecked(pcmAudioParameters.isLittleEndian);
    m_populating = false;
}

void MetadataEditorDialog::emitChanged()
{
    if (m_populating) return;
    emit videoParametersChanged(m_videoParameters);
    emit pcmAudioParametersChanged(m_pcmAudioParameters);
}

// --- Slots ---

void MetadataEditorDialog::onSystemChanged(int index)
{
    if (m_populating || index < 0) return;
    m_videoParameters.system = static_cast<VideoSystem>(ui->systemComboBox->itemData(index).toInt());
    populateChromaDecoderCombo(m_videoParameters.system);
    // Auto-select the appropriate default chroma decoder for the new system:
    // SECAM/MESECAM -> "secam", PAL/PAL-M -> "transform2d", NTSC -> "ntsc2d".
    if (m_videoParameters.system == SECAM || m_videoParameters.system == MESECAM) {
        m_videoParameters.chromaDecoder = QStringLiteral("secam");
    } else if (m_videoParameters.system == PAL || m_videoParameters.system == PAL_M) {
        if (m_videoParameters.chromaDecoder.isEmpty()
            || m_videoParameters.chromaDecoder.toLower() == QStringLiteral("secam")) {
            m_videoParameters.chromaDecoder = QStringLiteral("transform2d");
        }
    } else if (m_videoParameters.system == NTSC) {
        if (m_videoParameters.chromaDecoder.isEmpty()
            || m_videoParameters.chromaDecoder.toLower() == QStringLiteral("secam")) {
            m_videoParameters.chromaDecoder = QStringLiteral("ntsc2d");
        }
    }
    // Update the combo to reflect the auto-selected decoder.
    const int decoderIndex = ui->chromaDecoderComboBox->findData(m_videoParameters.chromaDecoder.toLower());
    if (decoderIndex >= 0) {
        QSignalBlocker blocker(ui->chromaDecoderComboBox);
        ui->chromaDecoderComboBox->setCurrentIndex(decoderIndex);
    }
    emitChanged();
}

void MetadataEditorDialog::onChromaDecoderChanged(int index)
{
    if (m_populating || index < 0) return;
    m_videoParameters.chromaDecoder = ui->chromaDecoderComboBox->itemData(index).toString();
    emitChanged();
}

void MetadataEditorDialog::onWidescreenToggled(bool checked)
{
    if (m_populating) return;
    m_videoParameters.isWidescreen = checked;
    emitChanged();
}

void MetadataEditorDialog::onSubcarrierLockedToggled(bool checked)
{
    if (m_populating) return;
    m_videoParameters.isSubcarrierLocked = checked;
    emitChanged();
}

void MetadataEditorDialog::onMappedToggled(bool checked)
{
    if (m_populating) return;
    m_videoParameters.isMapped = checked;
    emitChanged();
}

void MetadataEditorDialog::onTapeFormatChanged(const QString &text)
{
    if (m_populating) return;
    m_videoParameters.tapeFormat = text;
    emitChanged();
}

void MetadataEditorDialog::onChromaGainChanged(double value)
{
    if (m_populating) return;
    m_videoParameters.chromaGain = value;
    emitChanged();
}

void MetadataEditorDialog::onChromaPhaseChanged(double value)
{
    if (m_populating) return;
    m_videoParameters.chromaPhase = value;
    emitChanged();
}

void MetadataEditorDialog::onLumaNrChanged(double value)
{
    if (m_populating) return;
    m_videoParameters.lumaNR = value;
    emitChanged();
}

void MetadataEditorDialog::onNtscAdaptiveToggled(bool checked)
{
    if (m_populating) return;
    m_videoParameters.ntscAdaptive = checked ? 1 : 0;
    emitChanged();
}

void MetadataEditorDialog::onNtscAdaptThresholdChanged(double value)
{
    if (m_populating) return;
    m_videoParameters.ntscAdaptThreshold = value;
    emitChanged();
}

void MetadataEditorDialog::onNtscChromaWeightChanged(double value)
{
    if (m_populating) return;
    m_videoParameters.ntscChromaWeight = value;
    emitChanged();
}

void MetadataEditorDialog::onNtscPhaseCompensationToggled(bool checked)
{
    if (m_populating) return;
    m_videoParameters.ntscPhaseCompensation = checked ? 1 : 0;
    emitChanged();
}

void MetadataEditorDialog::onPalTransformThresholdChanged(double value)
{
    if (m_populating) return;
    m_videoParameters.palTransformThreshold = value;
    emitChanged();
}

void MetadataEditorDialog::onSampleRateChanged(double value)
{
    if (m_populating) return;
    m_videoParameters.sampleRate = value;
    emitChanged();
}

void MetadataEditorDialog::onColourBurstStartChanged(int value)
{
    if (m_populating) return;
    m_videoParameters.colourBurstStart = value;
    emitChanged();
}

void MetadataEditorDialog::onColourBurstEndChanged(int value)
{
    if (m_populating) return;
    m_videoParameters.colourBurstEnd = value;
    emitChanged();
}

void MetadataEditorDialog::onGitBranchChanged(const QString &text)
{
    if (m_populating) return;
    m_videoParameters.gitBranch = text;
    emitChanged();
}

void MetadataEditorDialog::onGitCommitChanged(const QString &text)
{
    if (m_populating) return;
    m_videoParameters.gitCommit = text;
    emitChanged();
}

void MetadataEditorDialog::onPcmSampleRateChanged(double value)
{
    if (m_populating) return;
    m_pcmAudioParameters.sampleRate = value;
    emitChanged();
}

void MetadataEditorDialog::onPcmBitsChanged(int value)
{
    if (m_populating) return;
    m_pcmAudioParameters.bits = value;
    emitChanged();
}

void MetadataEditorDialog::onPcmSignedToggled(bool checked)
{
    if (m_populating) return;
    m_pcmAudioParameters.isSigned = checked;
    emitChanged();
}

void MetadataEditorDialog::onPcmLittleEndianToggled(bool checked)
{
    if (m_populating) return;
    m_pcmAudioParameters.isLittleEndian = checked;
    emitChanged();
}

void MetadataEditorDialog::onApplyClicked()
{
    emitChanged();
    emit refreshRequested();
}

void MetadataEditorDialog::onOkClicked()
{
    emitChanged();
    accept();
}

void MetadataEditorDialog::onCancelClicked()
{
    reject();
}
