/******************************************************************************
 * metadataeditordialog.h
 * tbc-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Harry Munday
 *
 * This file is part of tbc-tools.
 *
 * MetadataEditorDialog is the Tools > Metadata Editor dialog. It exposes all
 * TbcMetaData::VideoParameters + PcmAudioParameters fields (except SNR, which
 * is per-field VitsMetrics) for manual editing — e.g. changing the TV System
 * from PAL to SECAM/MESECAM. Edits are applied live to the loaded source
 * (marking Save Metadata dirty), like VideoParametersDialog does.
 ******************************************************************************/

#ifndef METADATAEDITORDIALOG_H
#define METADATAEDITORDIALOG_H

#include <QDialog>
#include "tbcmetadata.h"

namespace Ui {
class MetadataEditorDialog;
}

class MetadataEditorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MetadataEditorDialog(QWidget *parent = nullptr);
    ~MetadataEditorDialog() override;

    void setVideoParameters(const TbcMetaData::VideoParameters &videoParameters);
    void setPcmAudioParameters(const TbcMetaData::PcmAudioParameters &pcmAudioParameters);
    void setSecamFieldContext(qint32 fieldNumber, bool firstLineIsRed, bool isSecamFamily);

signals:
    void videoParametersChanged(const TbcMetaData::VideoParameters &videoParameters);
    void pcmAudioParametersChanged(const TbcMetaData::PcmAudioParameters &pcmAudioParameters);
    void refreshRequested();
    void secamFirstLineIsRedChanged(qint32 fieldNumber, bool value, bool applyToAll);

private slots:
    void onSystemChanged(int index);
    void onChromaDecoderChanged(int index);
    void onWidescreenToggled(bool checked);
    void onSubcarrierLockedToggled(bool checked);
    void onMappedToggled(bool checked);
    void onTapeFormatChanged(const QString &text);
    void onChromaGainChanged(double value);
    void onChromaPhaseChanged(double value);
    void onLumaNrChanged(double value);
    void onNtscAdaptiveToggled(bool checked);
    void onNtscAdaptThresholdChanged(double value);
    void onNtscChromaWeightChanged(double value);
    void onNtscPhaseCompensationToggled(bool checked);
    void onPalTransformThresholdChanged(double value);
    void onSampleRateChanged(double value);
    void onColourBurstStartChanged(int value);
    void onColourBurstEndChanged(int value);
    void onGitBranchChanged(const QString &text);
    void onGitCommitChanged(const QString &text);
    void onPcmSampleRateChanged(double value);
    void onPcmBitsChanged(int value);
    void onPcmSignedToggled(bool checked);
    void onPcmLittleEndianToggled(bool checked);
    void onSecamFirstLineIsRedToggled(bool checked);
    void onApplyClicked();
    void onOkClicked();
    void onCancelClicked();

private:
    void populateChromaDecoderCombo(VideoSystem system);
    void emitChanged();

    Ui::MetadataEditorDialog *ui;
    TbcMetaData::VideoParameters m_videoParameters;
    TbcMetaData::PcmAudioParameters m_pcmAudioParameters;
    bool m_populating = false;
    qint32 m_secamFieldNumber = 1;
    bool m_isSecamFamily = false;
};

#endif // METADATAEDITORDIALOG_H
