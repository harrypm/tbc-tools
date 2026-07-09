/******************************************************************************
 * chromadecoderconfigdialog.cpp
 * ld-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2019-2025 Simon Inns
 * SPDX-FileCopyrightText: 2020-2022 Adam Sampson
 *
 * This file is part of ld-decode-tools.
 ******************************************************************************/

#include "chromadecoderconfigdialog.h"
#include "ui_chromadecoderconfigdialog.h"
#include "mainwindow.h"
#include <QEvent>
#include <QList>
#include <QSignalBlocker>

#include <cmath>

/*
 * These two functions provide a non-linear mapping for sliders that control
 * phase adjustments in degrees. The maximum range is from -180 to +180
 * degrees, but phase errors are usually < 10 degrees so we need more precise
 * adjustment in the middle.
 */

static constexpr double DEGREE_SLIDER_POWER = 3.0;
static constexpr qint32 DEGREE_SLIDER_SCALE = 1000;
static constexpr qint32 WHITE_LEVEL_MIN = 0x8000;
static constexpr qint32 WHITE_LEVEL_MAX = 0xFFFF;

static double degreesToSliderPos(double degrees) {
    double sliderPos = pow(abs(degrees) / 180, 1 / DEGREE_SLIDER_POWER) * DEGREE_SLIDER_SCALE;
    if (degrees < 0) {
        return -sliderPos;
    } else {
        return sliderPos;
    }
}

static double sliderPosToDegrees(double sliderPos) {
    double degrees = pow(abs(sliderPos) / DEGREE_SLIDER_SCALE, DEGREE_SLIDER_POWER) * 180;
    if (sliderPos < 0) {
        return -degrees;
    } else {
        return degrees;
    }
}

static qint32 whiteSliderToLevel(qint32 sliderValue)
{
    const qint32 clamped = qBound(WHITE_LEVEL_MIN, sliderValue, WHITE_LEVEL_MAX);
    return (WHITE_LEVEL_MIN + WHITE_LEVEL_MAX) - clamped;
}

static qint32 whiteLevelToSlider(qint32 whiteLevel)
{
    const qint32 clamped = qBound(WHITE_LEVEL_MIN, whiteLevel, WHITE_LEVEL_MAX);
    return (WHITE_LEVEL_MIN + WHITE_LEVEL_MAX) - clamped;
}

ChromaDecoderConfigDialog::ChromaDecoderConfigDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ChromaDecoderConfigDialog)
{
    ui->setupUi(this);
    setWindowFlags(Qt::Window);
    ui->blackLevelHorizontalSlider->setMinimum(0);
    ui->blackLevelHorizontalSlider->setMaximum(0x7FFF);
    ui->whiteLevelHorizontalSlider->setMinimum(WHITE_LEVEL_MIN);
    ui->whiteLevelHorizontalSlider->setMaximum(WHITE_LEVEL_MAX);
    ui->blackLevelSpinBox->setMinimum(0);
    ui->blackLevelSpinBox->setMaximum(0x7FFF);
    ui->blackLevelSpinBox->setSingleStep(1);
    ui->blackLevelSpinBox->setDisplayIntegerBase(10);
    ui->whiteLevelSpinBox->setMinimum(WHITE_LEVEL_MIN);
    ui->whiteLevelSpinBox->setMaximum(WHITE_LEVEL_MAX);
    ui->whiteLevelSpinBox->setSingleStep(1);
    ui->whiteLevelSpinBox->setDisplayIntegerBase(10);

    ui->chromaGainHorizontalSlider->setMinimum(0);
    ui->chromaGainHorizontalSlider->setMaximum(200);

    ui->chromaPhaseHorizontalSlider->setMinimum(-DEGREE_SLIDER_SCALE);
    ui->chromaPhaseHorizontalSlider->setMaximum(DEGREE_SLIDER_SCALE);

    ui->thresholdHorizontalSlider->setMinimum(0);
    ui->thresholdHorizontalSlider->setMaximum(100);

    ui->cNRHorizontalSlider->setMinimum(0);
    ui->cNRHorizontalSlider->setMaximum(100);

    ui->adaptThresholdHorizontalSlider->setMinimum(10);
    ui->adaptThresholdHorizontalSlider->setMaximum(200);

    ui->chromaWeightHorizontalSlider->setMinimum(0);
    ui->chromaWeightHorizontalSlider->setMaximum(200);

    ui->yNRHorizontalSlider->setMinimum(0);
    ui->yNRHorizontalSlider->setMaximum(100);

    const QList<QSlider *> resettableSliders = {
        ui->blackLevelHorizontalSlider,
        ui->whiteLevelHorizontalSlider,
        ui->chromaGainHorizontalSlider,
        ui->chromaPhaseHorizontalSlider,
        ui->yNRHorizontalSlider,
        ui->thresholdHorizontalSlider,
        ui->adaptThresholdHorizontalSlider,
        ui->chromaWeightHorizontalSlider,
        ui->cNRHorizontalSlider
    };
    for (QSlider *slider : resettableSliders) {
        if (slider) {
            slider->installEventFilter(this);
        }
    }
    
	//get tbcSource instance from mainWindow
	if (auto mw = qobject_cast<MainWindow*>(parent)) {
		tbcSource = &mw->getTbcSource();
	}
	
    // Update the dialogue
    updateDialog();
}

ChromaDecoderConfigDialog::~ChromaDecoderConfigDialog()
{
    delete ui;
}

void ChromaDecoderConfigDialog::setConfiguration(VideoSystem _system, const PalColour::Configuration &_palConfiguration,
                                                 const Comb::Configuration &_ntscConfiguration,
                                                 const MonoDecoder::MonoConfiguration &_monoConfiguration,
												 const TbcSource::SourceMode &_mode,
												 const bool _isInit)
{
    const double configuredYNRLevel = _system == NTSC ? _ntscConfiguration.yNRLevel : _palConfiguration.yNRLevel;
    system = _system;
    palConfiguration = _palConfiguration;
    ntscConfiguration = _ntscConfiguration;
    monoConfiguration = _monoConfiguration;
	sourceMode = _mode;

    palConfiguration.chromaGain = qBound(0.0, palConfiguration.chromaGain, 2.0);
    palConfiguration.chromaPhase = qBound(-180.0, palConfiguration.chromaPhase, 180.0);
    palConfiguration.transformThreshold = qBound(0.0, palConfiguration.transformThreshold, 1.0);
    palConfiguration.yNRLevel = qBound(0.0, configuredYNRLevel, 10.0);
    ntscConfiguration.cNRLevel = qBound(0.0, ntscConfiguration.cNRLevel, 10.0);
    ntscConfiguration.yNRLevel = qBound(0.0, configuredYNRLevel, 10.0);
    monoConfiguration.yNRLevel = qBound(0.0, configuredYNRLevel, 10.0);

    ntscConfiguration.adaptThreshold = qBound(0.1, ntscConfiguration.adaptThreshold, 2.0);
    ntscConfiguration.chromaWeight = qBound(0.0, ntscConfiguration.chromaWeight, 2.0);    

    // For settings that both decoders share, the PAL default takes precedence
    ntscConfiguration.chromaGain = palConfiguration.chromaGain;
    ntscConfiguration.chromaPhase = palConfiguration.chromaPhase;

    // Select the tab corresponding to the current standard automatically
    if (system == NTSC) {
        ui->standardTabs->setCurrentWidget(ui->ntscTab);
    } else {
        ui->standardTabs->setCurrentWidget(ui->palTab);
    }
	
	isInit = _isInit;
    updateDialog();
    emit chromaDecoderConfigChanged();
}

const PalColour::Configuration &ChromaDecoderConfigDialog::getPalConfiguration()
{
    return palConfiguration;
}

const Comb::Configuration &ChromaDecoderConfigDialog::getNtscConfiguration()
{
    return ntscConfiguration;
}

void ChromaDecoderConfigDialog::setVideoLevels(const LdDecodeMetaData::VideoParameters &videoParameters)
{
    system = videoParameters.system;

    if (videoParameters.black16bIre >= 0) {
        blackLevel = qBound(0, videoParameters.black16bIre, 0x7FFF);
    }
    if (videoParameters.white16bIre >= 0) {
        whiteLevel = qBound(0x8000, videoParameters.white16bIre, 0xFFFF);
    }

    startingBlackLevel = blackLevel;
    startingWhiteLevel = whiteLevel;

    updatingLevels = true;
    QSignalBlocker blackSliderBlocker(ui->blackLevelHorizontalSlider);
    QSignalBlocker whiteSliderBlocker(ui->whiteLevelHorizontalSlider);
    QSignalBlocker blackSpinBlocker(ui->blackLevelSpinBox);
    QSignalBlocker whiteSpinBlocker(ui->whiteLevelSpinBox);
    QSignalBlocker blackResetBlocker(ui->blackLevelResetComboBox);
    QSignalBlocker whiteResetBlocker(ui->whiteLevelResetComboBox);
    if (blackLevel >= 0) {
        ui->blackLevelHorizontalSlider->setValue(blackLevel);
    }
    if (whiteLevel >= 0) {
        ui->whiteLevelHorizontalSlider->setValue(whiteLevelToSlider(whiteLevel));
    }
    ui->blackLevelResetComboBox->setCurrentIndex(0);
    ui->whiteLevelResetComboBox->setCurrentIndex(0);
    updatingLevels = false;

    updateLevelLabels();
}

QString ChromaDecoderConfigDialog::formatLevelSuffix(qint32 value) const
{
    if (value < 0) {
        return QString();
    }
    return QStringLiteral(" (0x%1)").arg(value, 4, 16, QChar('0'));
}

void ChromaDecoderConfigDialog::updateLevelLabels()
{
    QSignalBlocker blackSpinBlocker(ui->blackLevelSpinBox);
    QSignalBlocker whiteSpinBlocker(ui->whiteLevelSpinBox);

    if (blackLevel >= 0) {
        ui->blackLevelSpinBox->setEnabled(true);
        ui->blackLevelSpinBox->setValue(blackLevel);
        ui->blackLevelSpinBox->setSuffix(formatLevelSuffix(blackLevel));
    } else {
        ui->blackLevelSpinBox->setEnabled(false);
        ui->blackLevelSpinBox->setSuffix(QString());
    }

    if (whiteLevel >= 0) {
        ui->whiteLevelSpinBox->setEnabled(true);
        ui->whiteLevelSpinBox->setValue(whiteLevel);
        ui->whiteLevelSpinBox->setSuffix(formatLevelSuffix(whiteLevel));
    } else {
        ui->whiteLevelSpinBox->setEnabled(false);
        ui->whiteLevelSpinBox->setSuffix(QString());
    }
}

qint32 ChromaDecoderConfigDialog::levelForResetIndex(int index, bool white) const
{
    if (index == 0) {
        if (white) {
            return (startingWhiteLevel >= 0) ? startingWhiteLevel : whiteLevel;
        }
        return (startingBlackLevel >= 0) ? startingBlackLevel : blackLevel;
    }

    if (white) {
        if (system == NTSC) {
            return 0xC800;
        }
        return 0xD300;
    }

    if (system == NTSC) {
        return (index == 2) ? 0x3C00 : 0x3C00 + 0x0A80;
    }

    return 0x4000;
}

void ChromaDecoderConfigDialog::updateDialog()
{
    const bool isSourcePal = system == PAL || system == PAL_M;
    const bool isSourceNtsc = system == NTSC;
	
	if(!isInit)
	{
		if(sourceMode == TbcSource::ONE_SOURCE)
		{
			palConfiguration.chromaFilter = PalColour::transform3DFilter;
			ntscConfiguration.dimensions = 3;
            ntscConfiguration.nnTransform3D = false;
		}
		else
		{
			palConfiguration.chromaFilter = PalColour::transform2DFilter;
			ntscConfiguration.dimensions = 2;
            ntscConfiguration.nnTransform3D = false;
		}
        if (tbcSource) {
            const auto &videoParameters = tbcSource->getVideoParameters();
            if (videoParameters.ntscPhaseCompensation >= 0) {
                ntscConfiguration.phaseCompensation = (videoParameters.ntscPhaseCompensation != 0);
            } else {
                ntscConfiguration.phaseCompensation = true;
            }
        } else {
            ntscConfiguration.phaseCompensation = true;
        }
		ui->enableYCCombineCheckBox->setChecked(combine);
		
		isInit = true;
	}

    // Shared settings

    ui->chromaGainHorizontalSlider->setValue(static_cast<qint32>(palConfiguration.chromaGain * 100));

    ui->chromaGainValueLabel->setEnabled(true);
    ui->chromaGainValueLabel->setText(QString::number(palConfiguration.chromaGain, 'f', 2));

    ui->chromaPhaseHorizontalSlider->setValue(static_cast<qint32>(degreesToSliderPos(palConfiguration.chromaPhase)));

    ui->chromaPhaseValueLabel->setEnabled(true);
    ui->chromaPhaseValueLabel->setText(QString::number(palConfiguration.chromaPhase, 'f', 1) + QChar(0xB0));
    
    const double currentYNRLevel = (system == NTSC) ? ntscConfiguration.yNRLevel : palConfiguration.yNRLevel;
    ui->yNRHorizontalSlider->setValue(static_cast<qint32>(currentYNRLevel * 10));
    ui->yNRValueLabel->setText(QString::number(currentYNRLevel, 'f', 1) + tr(" IRE"));
	
	if(sourceMode == TbcSource::BOTH_SOURCES)
	{
		ui->enableYCCombineCheckBox->show();
	}
	else
	{
		ui->enableYCCombineCheckBox->hide();
	}

    // PAL settings
	
	ui->palMonoRadioButton->setEnabled(isSourcePal);
    ui->palFilterPalColourRadioButton->setEnabled(isSourcePal);
    ui->palFilterTransform2DRadioButton->setEnabled(isSourcePal);
    ui->palFilterTransform3DRadioButton->setEnabled(isSourcePal);
	
	if(isSourcePal)
	{
		switch (palConfiguration.chromaFilter) {
		case PalColour::mono:
			ui->palMonoRadioButton->setChecked(true);
			ui->chromaGainHorizontalSlider->setEnabled(false);
			ui->chromaPhaseHorizontalSlider->setEnabled(false);
			break;
		case PalColour::palColourFilter:
			ui->palFilterPalColourRadioButton->setChecked(true);
			ui->chromaGainHorizontalSlider->setEnabled(true);
			ui->chromaPhaseHorizontalSlider->setEnabled(true);
			break;
		case PalColour::transform2DFilter:
			ui->palFilterTransform2DRadioButton->setChecked(true);
			ui->chromaGainHorizontalSlider->setEnabled(true);
			ui->chromaPhaseHorizontalSlider->setEnabled(true);
			break;
		case PalColour::transform3DFilter:
			ui->palFilterTransform3DRadioButton->setChecked(true);
			ui->chromaGainHorizontalSlider->setEnabled(true);
			ui->chromaPhaseHorizontalSlider->setEnabled(true);
			break;
		}
	}

    const bool isTransform = ((palConfiguration.chromaFilter != PalColour::palColourFilter) && (palConfiguration.chromaFilter != PalColour::mono) );

    ui->thresholdLabel->setEnabled(isSourcePal && isTransform);

    ui->thresholdHorizontalSlider->setEnabled(isSourcePal && isTransform);
    ui->thresholdHorizontalSlider->setValue(static_cast<qint32>(palConfiguration.transformThreshold * 100));

    ui->thresholdValueLabel->setEnabled(isSourcePal && isTransform);
    ui->thresholdValueLabel->setText(QString::number(palConfiguration.transformThreshold, 'f', 2));

    ui->showFFTsCheckBox->setEnabled(isSourcePal && isTransform);
    ui->showFFTsCheckBox->setChecked(palConfiguration.showFFTs);

    ui->simplePALCheckBox->setEnabled(isSourcePal && isTransform);
    ui->simplePALCheckBox->setChecked(palConfiguration.simplePAL);

    // NTSC settings

    ui->phaseCompCheckBox->setEnabled(isSourceNtsc);
    ui->phaseCompCheckBox->setChecked(ntscConfiguration.phaseCompensation);
    ui->ntscMonoRadioButton->setEnabled(isSourceNtsc);
    ui->ntscFilter1DRadioButton->setEnabled(isSourceNtsc);
    ui->ntscFilter2DRadioButton->setEnabled(isSourceNtsc);
    ui->ntscFilter3DRadioButton->setEnabled(isSourceNtsc);
    ui->ntscFilterNNTransform3DRadioButton->setEnabled(isSourceNtsc);
	
	if(isSourceNtsc)
	{
		switch (ntscConfiguration.dimensions) {
		case 0:
			ui->ntscMonoRadioButton->setChecked(true);
			ui->phaseCompCheckBox->setEnabled(false);
			ui->chromaGainHorizontalSlider->setEnabled(false);
			ui->chromaPhaseHorizontalSlider->setEnabled(false);
			break;
		case 1:
			ui->ntscFilter1DRadioButton->setChecked(true);
			ui->phaseCompCheckBox->setEnabled(true);
			ui->chromaGainHorizontalSlider->setEnabled(true);
			ui->chromaPhaseHorizontalSlider->setEnabled(true);
			break;
		case 2:
			ui->ntscFilter2DRadioButton->setChecked(true);
			ui->phaseCompCheckBox->setEnabled(true);
			ui->chromaGainHorizontalSlider->setEnabled(true);
			ui->chromaPhaseHorizontalSlider->setEnabled(true);
			break;
		case 3:
            if (ntscConfiguration.nnTransform3D) {
                ui->ntscFilterNNTransform3DRadioButton->setChecked(true);
            } else {
                ui->ntscFilter3DRadioButton->setChecked(true);
            }
			ui->phaseCompCheckBox->setEnabled(true);
			ui->chromaGainHorizontalSlider->setEnabled(true);
			ui->chromaPhaseHorizontalSlider->setEnabled(true);
			break;
		}
    }

    const bool isClassic3DMode = isSourceNtsc
                                 && ntscConfiguration.dimensions == 3
                                 && !ntscConfiguration.nnTransform3D;
    ui->adaptiveCheckBox->setEnabled(isClassic3DMode);
    ui->adaptiveCheckBox->setChecked(ntscConfiguration.adaptive);

    ui->showMapCheckBox->setEnabled(isClassic3DMode);
    ui->showMapCheckBox->setChecked(ntscConfiguration.showMap);

    ui->adaptThresholdLabel->setEnabled(isClassic3DMode);
    ui->adaptThresholdHorizontalSlider->setEnabled(isClassic3DMode);
    ui->adaptThresholdHorizontalSlider->setValue(static_cast<qint32>(ntscConfiguration.adaptThreshold * 100));
    ui->adaptThresholdValueLabel->setEnabled(isClassic3DMode);
    ui->adaptThresholdValueLabel->setText(QString::number(ntscConfiguration.adaptThreshold, 'f', 2));

    ui->chromaWeightLabel->setEnabled(isClassic3DMode);
    ui->chromaWeightHorizontalSlider->setEnabled(isClassic3DMode);
    ui->chromaWeightHorizontalSlider->setValue(static_cast<qint32>(ntscConfiguration.chromaWeight * 100));
    ui->chromaWeightValueLabel->setEnabled(isClassic3DMode);
    ui->chromaWeightValueLabel->setText(QString::number(ntscConfiguration.chromaWeight, 'f', 2));

    ui->cNRLabel->setEnabled(isSourceNtsc);

    ui->cNRHorizontalSlider->setEnabled(isSourceNtsc && ntscConfiguration.dimensions != 0);
    ui->cNRHorizontalSlider->setValue(static_cast<qint32>(ntscConfiguration.cNRLevel * 10));

    ui->cNRValueLabel->setEnabled(isSourceNtsc);
    ui->cNRValueLabel->setText(QString::number(ntscConfiguration.cNRLevel, 'f', 1) + tr(" IRE"));

    updateLevelLabels();
}
bool ChromaDecoderConfigDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (event && event->type() == QEvent::MouseButtonDblClick) {
        if (QSlider *slider = qobject_cast<QSlider *>(watched)) {
            resetSliderToDefault(slider);
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void ChromaDecoderConfigDialog::resetSliderToDefault(QSlider *slider)
{
    if (!slider) {
        return;
    }

    if (slider == ui->blackLevelHorizontalSlider) {
        slider->setValue(levelForResetIndex(0, false));
    } else if (slider == ui->whiteLevelHorizontalSlider) {
        slider->setValue(whiteLevelToSlider(levelForResetIndex(0, true)));
    } else if (slider == ui->chromaGainHorizontalSlider) {
        slider->setValue(100);
    } else if (slider == ui->chromaPhaseHorizontalSlider) {
        slider->setValue(static_cast<int>(degreesToSliderPos(0.0)));
    } else if (slider == ui->yNRHorizontalSlider) {
        slider->setValue(0);
    } else if (slider == ui->thresholdHorizontalSlider) {
        slider->setValue(40);
    } else if (slider == ui->adaptThresholdHorizontalSlider) {
        slider->setValue(100);
    } else if (slider == ui->chromaWeightHorizontalSlider) {
        slider->setValue(100);
    } else if (slider == ui->cNRHorizontalSlider) {
        slider->setValue(0);
    }
}

// Methods to handle changes to the dialogue
void ChromaDecoderConfigDialog::on_blackLevelHorizontalSlider_valueChanged(int value)
{
    blackLevel = value;
    updateLevelLabels();
    if (!updatingLevels) {
        emit videoLevelsChanged(blackLevel, whiteLevel);
    }
}

void ChromaDecoderConfigDialog::on_whiteLevelHorizontalSlider_valueChanged(int value)
{
    whiteLevel = whiteSliderToLevel(value);
    updateLevelLabels();
    if (!updatingLevels) {
        emit videoLevelsChanged(blackLevel, whiteLevel);
    }
}
void ChromaDecoderConfigDialog::on_blackLevelSpinBox_valueChanged(int value)
{
    blackLevel = qBound(0, value, 0x7FFF);
    updateLevelLabels();
    if (!updatingLevels) {
        QSignalBlocker sliderBlocker(ui->blackLevelHorizontalSlider);
        ui->blackLevelHorizontalSlider->setValue(blackLevel);
        emit videoLevelsChanged(blackLevel, whiteLevel);
    }
}

void ChromaDecoderConfigDialog::on_whiteLevelSpinBox_valueChanged(int value)
{
    whiteLevel = qBound(WHITE_LEVEL_MIN, value, WHITE_LEVEL_MAX);
    updateLevelLabels();
    if (!updatingLevels) {
        QSignalBlocker sliderBlocker(ui->whiteLevelHorizontalSlider);
        ui->whiteLevelHorizontalSlider->setValue(whiteLevelToSlider(whiteLevel));
        emit videoLevelsChanged(blackLevel, whiteLevel);
    }
}

void ChromaDecoderConfigDialog::on_blackLevelResetComboBox_activated(int index)
{
    const qint32 resetValue = levelForResetIndex(index, false);
    if (resetValue >= 0) {
        ui->blackLevelHorizontalSlider->setValue(resetValue);
    }
}

void ChromaDecoderConfigDialog::on_whiteLevelResetComboBox_activated(int index)
{
    const qint32 resetValue = levelForResetIndex(index, true);
    if (resetValue >= 0) {
        ui->whiteLevelHorizontalSlider->setValue(whiteLevelToSlider(resetValue));
    }
}

void ChromaDecoderConfigDialog::on_chromaGainHorizontalSlider_valueChanged(int value)
{
    palConfiguration.chromaGain = static_cast<double>(value) / 100;
    ntscConfiguration.chromaGain = palConfiguration.chromaGain;
    ui->chromaGainValueLabel->setText(QString::number(palConfiguration.chromaGain, 'f', 2));
    emit chromaDecoderConfigChanged();
}

void ChromaDecoderConfigDialog::on_chromaPhaseHorizontalSlider_valueChanged(int value)
{
    palConfiguration.chromaPhase = sliderPosToDegrees(static_cast<double>(value));
    ntscConfiguration.chromaPhase = palConfiguration.chromaPhase;
    ui->chromaPhaseValueLabel->setText(QString::number(palConfiguration.chromaPhase, 'f', 1) + QChar(0xB0));
    emit chromaDecoderConfigChanged();
}


void ChromaDecoderConfigDialog::on_palFilterButtonGroup_buttonClicked(QAbstractButton *button)
{
	if (button == ui->palMonoRadioButton){
		palConfiguration.chromaFilter = PalColour::mono;
    } else if (button == ui->palFilterPalColourRadioButton) {
        palConfiguration.chromaFilter = PalColour::palColourFilter;
    } else if (button == ui->palFilterTransform2DRadioButton) {
        palConfiguration.chromaFilter = PalColour::transform2DFilter;
    } else {
        palConfiguration.chromaFilter = PalColour::transform3DFilter;
    }
    updateDialog();
    emit chromaDecoderConfigChanged();
}

void ChromaDecoderConfigDialog::on_thresholdHorizontalSlider_valueChanged(int value)
{
    palConfiguration.transformThreshold = static_cast<double>(value) / 100;
    ui->thresholdValueLabel->setText(QString::number(palConfiguration.transformThreshold, 'f', 2));
    emit chromaDecoderConfigChanged();
}

void ChromaDecoderConfigDialog::on_showFFTsCheckBox_clicked()
{
    palConfiguration.showFFTs = ui->showFFTsCheckBox->isChecked();
    emit chromaDecoderConfigChanged();
}

void ChromaDecoderConfigDialog::on_simplePALCheckBox_clicked()
{
    palConfiguration.simplePAL = ui->simplePALCheckBox->isChecked();
    emit chromaDecoderConfigChanged();
}

void ChromaDecoderConfigDialog::on_ntscFilterButtonGroup_buttonClicked(QAbstractButton *button)
{
	if(button == ui->ntscMonoRadioButton){
		ntscConfiguration.dimensions = 0;
        ntscConfiguration.nnTransform3D = false;
	} else if (button == ui->ntscFilter1DRadioButton) {
        ntscConfiguration.dimensions = 1;
        ntscConfiguration.nnTransform3D = false;
    } else if (button == ui->ntscFilter2DRadioButton) {
        ntscConfiguration.dimensions = 2;
        ntscConfiguration.nnTransform3D = false;
    } else if (button == ui->ntscFilterNNTransform3DRadioButton) {
        ntscConfiguration.dimensions = 3;
        ntscConfiguration.nnTransform3D = true;
    } else {
        ntscConfiguration.dimensions = 3;
        ntscConfiguration.nnTransform3D = false;
    }
    updateDialog();
    emit chromaDecoderConfigChanged();
}

void ChromaDecoderConfigDialog::on_phaseCompCheckBox_clicked()
{
    ntscConfiguration.phaseCompensation = ui->phaseCompCheckBox->isChecked();
    emit chromaDecoderConfigChanged();
}

void ChromaDecoderConfigDialog::on_adaptiveCheckBox_clicked()
{
    ntscConfiguration.adaptive = ui->adaptiveCheckBox->isChecked();
    emit chromaDecoderConfigChanged();
}

void ChromaDecoderConfigDialog::on_showMapCheckBox_clicked()
{
    ntscConfiguration.showMap = ui->showMapCheckBox->isChecked();
    emit chromaDecoderConfigChanged();
}

void ChromaDecoderConfigDialog::on_adaptThresholdHorizontalSlider_valueChanged(int value)
{
    ntscConfiguration.adaptThreshold = static_cast<double>(value) / 100;
    ui->adaptThresholdValueLabel->setText(QString::number(ntscConfiguration.adaptThreshold, 'f', 2));
    emit chromaDecoderConfigChanged();
}

void ChromaDecoderConfigDialog::on_chromaWeightHorizontalSlider_valueChanged(int value)
{
    ntscConfiguration.chromaWeight = static_cast<double>(value) / 100;
    ui->chromaWeightValueLabel->setText(QString::number(ntscConfiguration.chromaWeight, 'f', 2));
    emit chromaDecoderConfigChanged();
}

void ChromaDecoderConfigDialog::on_cNRHorizontalSlider_valueChanged(int value)
{
    ntscConfiguration.cNRLevel = static_cast<double>(value) / 10;
    ui->cNRValueLabel->setText(QString::number(ntscConfiguration.cNRLevel, 'f', 1) + tr(" IRE"));
    emit chromaDecoderConfigChanged();
}

void ChromaDecoderConfigDialog::on_yNRHorizontalSlider_valueChanged(int value)
{
    const double ynrLevel = static_cast<double>(value) / 10;
    palConfiguration.yNRLevel = ynrLevel;
    ntscConfiguration.yNRLevel = ynrLevel;
	monoConfiguration.yNRLevel = ynrLevel;
    ui->yNRValueLabel->setText(QString::number(ynrLevel, 'f', 1) + tr(" IRE"));
    emit chromaDecoderConfigChanged();
}
void ChromaDecoderConfigDialog::levelSelected(qint32 level)
{
    if (level < 0x8000) {
        ui->blackLevelHorizontalSlider->setValue(level);
    } else {
        ui->whiteLevelHorizontalSlider->setValue(whiteLevelToSlider(level));
    }
}

void ChromaDecoderConfigDialog::updateSourceMode(TbcSource::SourceMode mode)
{
	sourceMode = mode;
	if(sourceMode == TbcSource::BOTH_SOURCES)
	{
		ui->enableYCCombineCheckBox->show();
	}
	else
	{
		ui->enableYCCombineCheckBox->hide();
	}
}

void ChromaDecoderConfigDialog::on_enableYCCombineCheckBox_clicked()
{
	combine = ui->enableYCCombineCheckBox->isChecked();
	tbcSource->setCombine(combine);
	emit chromaDecoderConfigChanged();
}
