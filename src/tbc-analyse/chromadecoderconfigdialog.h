/******************************************************************************
 * chromadecoderconfigdialog.h
 * tbc-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2019-2025 Simon Inns
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#ifndef CHROMADECODERCONFIGDIALOG_H
#define CHROMADECODERCONFIGDIALOG_H

#include <QAbstractButton>
#include <QDialog>
#include <QEvent>
#include <QSlider>
#include <QString>

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
    void setVideoLevels(const TbcMetaData::VideoParameters &videoParameters);
    const PalColour::Configuration &getPalConfiguration();
    const Comb::Configuration &getNtscConfiguration();
    // Provide the TbcSource the dialog interacts with (Y/C combine toggle,
    // NTSC phase-compensation default). MainWindow calls this right after
    // construction; replaced the previous parent-widget MainWindow cast,
    // which forced this dialog to include mainwindow.h and made it impossible
    // to compile standalone (e.g. for headless tests).
    void setTbcSource(TbcSource *source) { tbcSource = source; }
    qint8 getSecamPredemodFirstLineIsRedOverride() const { return secamPredemodFirstLineIsRedOverride; }

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
    void on_secamButtonGroup_buttonClicked(QAbstractButton *button);
    void on_secamFirstLineIsRedComboBox_currentIndexChanged(int index);
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
    qint8 secamPredemodFirstLineIsRedOverride = -1;
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

// Derives the canonical chroma decoder name (as stored in
// VideoParameters.chromaDecoder and passed to tbc-video-export as
// --chroma-decoder) from the live decoder configurations. Covers every
// decoder mode: PAL (pal2d/transform2d/transform3d/mono), SECAM
// (secam/secam-predemod/mono) and NTSC
// (ntsc1d/ntsc2d/ntsc3d/ntsc3dnoadapt/nntransform3d/mono). The NTSC 3D
// adaptive state is expressed through the decoder name (ntsc3dnoadapt),
// because tbc-chroma-decoder ignores the metadata ntscAdaptive field once
// -f selects the decoder explicitly. Returns an empty string when the
// configurations do not map to a named decoder for the system, in which
// case callers must leave any existing metadata value untouched. Lives
// here (not in mainwindow.cpp) so the load-sequence test can verify every
// mode's round trip.
inline QString chromaDecoderNameFromConfig(VideoSystem system,
                                           const PalColour::Configuration &palConfig,
                                           const Comb::Configuration &ntscConfig)
{
    const bool isPal = (system == PAL || system == PAL_M);
    const bool isSecam = (system == SECAM || system == MESECAM);
    if (isPal) {
        switch (palConfig.chromaFilter) {
        case PalColour::palColourFilter:
            return QStringLiteral("pal2d");
        case PalColour::transform2DFilter:
            return QStringLiteral("transform2d");
        case PalColour::transform3DFilter:
            return QStringLiteral("transform3d");
        case PalColour::mono:
            return QStringLiteral("mono");
        default:
            break;
        }
    } else if (isSecam) {
        // SECAM is its own system: only SECAM decoders (and mono) are named here.
        switch (palConfig.chromaFilter) {
        case PalColour::secam:
            return QStringLiteral("secam");
        case PalColour::secamPredemod:
            return QStringLiteral("secam-predemod");
        case PalColour::mono:
            return QStringLiteral("mono");
        default:
            break;
        }
    }

    if (system == NTSC) {
        if (ntscConfig.dimensions <= 0) {
            return QStringLiteral("mono");
        }
        switch (ntscConfig.dimensions) {
        case 1:
            return QStringLiteral("ntsc1d");
        case 2:
            return QStringLiteral("ntsc2d");
        case 3:
            if (ntscConfig.nnTransform3D) {
                return QStringLiteral("nntransform3d");
            }
            return ntscConfig.adaptive ? QStringLiteral("ntsc3d")
                                       : QStringLiteral("ntsc3dnoadapt");
        default:
            break;
        }
    }

    return QString();
}

#endif // CHROMADECODERCONFIGDIALOG_H
