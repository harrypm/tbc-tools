/******************************************************************************
 * chromadecoderconfigdialog.h
 * ld-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2019-2025 Simon Inns
 *
 * This file is part of ld-decode-tools.
 ******************************************************************************/

#ifndef CHROMADECODERCONFIGDIALOG_H
#define CHROMADECODERCONFIGDIALOG_H

#include <QAbstractButton>
#include <QDialog>
#include <QEvent>
#include <QSlider>

#include "comb.h"
#include "palcolour.h"
#include "monodecoder.h"
#include "tbcsource.h"

namespace Ui {
class ChromaDecoderConfigDialog;
}

class ChromaDecoderConfigDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ChromaDecoderConfigDialog(QWidget *parent = nullptr);
    ~ChromaDecoderConfigDialog();

    void setConfiguration(VideoSystem system, const PalColour::Configuration &palConfiguration,
                          const Comb::Configuration &ntscConfiguration,
                          const MonoDecoder::MonoConfiguration &monoConfiguration,
                          const TbcSource::SourceMode &_mode,
						  const bool _isInit);
    void setVideoLevels(const LdDecodeMetaData::VideoParameters &videoParameters);
    const PalColour::Configuration &getPalConfiguration();
    const Comb::Configuration &getNtscConfiguration();

signals:
    void chromaDecoderConfigChanged();
    void videoLevelsChanged(qint32 blackLevel, qint32 whiteLevel);

public slots:
	void updateSourceMode(TbcSource::SourceMode mode);
    void levelSelected(qint32 level);

private slots:
    void on_blackLevelHorizontalSlider_valueChanged(int value);
    void on_whiteLevelHorizontalSlider_valueChanged(int value);
    void on_blackLevelSpinBox_valueChanged(int value);
    void on_whiteLevelSpinBox_valueChanged(int value);
    void on_blackLevelResetComboBox_activated(int index);
    void on_whiteLevelResetComboBox_activated(int index);
    void on_chromaGainHorizontalSlider_valueChanged(int value);
    void on_chromaPhaseHorizontalSlider_valueChanged(int value);
	void on_enableYCCombineCheckBox_clicked();

    void on_palFilterButtonGroup_buttonClicked(QAbstractButton *button);
    void on_thresholdHorizontalSlider_valueChanged(int value);
    void on_showFFTsCheckBox_clicked();
    void on_simplePALCheckBox_clicked();

    void on_ntscFilterButtonGroup_buttonClicked(QAbstractButton *button);
    void on_phaseCompCheckBox_clicked();
    void on_adaptiveCheckBox_clicked();
    void on_showMapCheckBox_clicked();
    void on_adaptThresholdHorizontalSlider_valueChanged(int value);
    void on_chromaWeightHorizontalSlider_valueChanged(int value);    
    void on_cNRHorizontalSlider_valueChanged(int value);
    void on_yNRHorizontalSlider_valueChanged(int value);

private:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resetSliderToDefault(QSlider *slider);
    Ui::ChromaDecoderConfigDialog *ui;
    VideoSystem system;
    PalColour::Configuration palConfiguration;
    Comb::Configuration ntscConfiguration;
    MonoDecoder::MonoConfiguration monoConfiguration;
	TbcSource* tbcSource = nullptr;
	TbcSource::SourceMode sourceMode;
	bool isInit = true;
	bool combine = false;
    qint32 blackLevel = -1;
    qint32 whiteLevel = -1;
    qint32 startingBlackLevel = -1;
    qint32 startingWhiteLevel = -1;
    bool updatingLevels = false;

    void updateDialog();
    void updateLevelLabels();
    qint32 levelForResetIndex(int index, bool white) const;
    QString formatLevelSuffix(qint32 value) const;
};

#endif // CHROMADECODERCONFIGDIALOG_H
