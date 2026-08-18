/******************************************************************************
 * yuvrangedialog.h
 * tbc-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#ifndef YUVRANGEDIALOG_H
#define YUVRANGEDIALOG_H

#include <QDialog>
#include <QImage>
#include <QSize>
#include <QtGlobal>

class QLabel;
class QPushButton;
class QResizeEvent;
class QSlider;
class QSpinBox;

struct YuvRangeSettings {
    qint32 lumaMin = 16;
    qint32 lumaMax = 235;
    qint32 chromaMin = 16;
    qint32 chromaMax = 240;
    qint32 overlayAlpha = 180;
    bool overlayEnabled = false;
};

class YuvRangeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit YuvRangeDialog(QWidget *parent = nullptr);
    ~YuvRangeDialog() override = default;

    void showScopeImage(const QImage &scopeImage);
    QSize scopeRenderTargetSize() const;
    YuvRangeSettings settings() const;

signals:
    void renderTargetSizeChanged(const QSize &targetSize);
    void settingsChanged(const YuvRangeSettings &settings);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateScopeLabelPixmap();
    void setSpinBoxValueWithoutSignals(QSpinBox *spinBox, qint32 value);
    void emitSettingsChanged();

    QLabel *scopeLabel_ = nullptr;
    QImage cachedScopeImage_;
    QSpinBox *lumaMinSpinBox_ = nullptr;
    QSpinBox *lumaMaxSpinBox_ = nullptr;
    QSpinBox *chromaMinSpinBox_ = nullptr;
    QSpinBox *chromaMaxSpinBox_ = nullptr;
    QPushButton *overlayButton_ = nullptr;
    QSlider *overlayAlphaSlider_ = nullptr;
    QLabel *overlayAlphaValueLabel_ = nullptr;
};

#endif // YUVRANGEDIALOG_H
