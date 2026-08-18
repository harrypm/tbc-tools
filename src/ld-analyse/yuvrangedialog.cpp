/******************************************************************************
 * yuvrangedialog.cpp
 * tbc-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#include "yuvrangedialog.h"

#include <QGridLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

YuvRangeDialog::YuvRangeDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("YUV Range"));
    setWindowFlags(Qt::Window);
    setModal(false);
    resize(960, 540);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 6, 6, 6);
    mainLayout->setSpacing(6);

    scopeLabel_ = new QLabel(this);
    scopeLabel_->setAlignment(Qt::AlignCenter);
    scopeLabel_->setMinimumSize(1, 1);
    scopeLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    scopeLabel_->setScaledContents(false);
    scopeLabel_->setText(tr("No YUV range data"));
    mainLayout->addWidget(scopeLabel_);

    auto *controlsLayout = new QGridLayout();
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    controlsLayout->setHorizontalSpacing(6);
    controlsLayout->setVerticalSpacing(4);

    auto makeSpinBox = [this](qint32 value) {
        auto *spinBox = new QSpinBox(this);
        spinBox->setRange(0, 255);
        spinBox->setValue(value);
        spinBox->setKeyboardTracking(false);
        return spinBox;
    };

    lumaMinSpinBox_ = makeSpinBox(16);
    lumaMaxSpinBox_ = makeSpinBox(235);
    chromaMinSpinBox_ = makeSpinBox(16);
    chromaMaxSpinBox_ = makeSpinBox(240);

    overlayButton_ = new QPushButton(tr("Overlay Off"), this);
    overlayButton_->setCheckable(true);
    overlayButton_->setChecked(false);
    overlayAlphaSlider_ = new QSlider(Qt::Horizontal, this);
    overlayAlphaSlider_->setRange(0, 255);
    overlayAlphaSlider_->setValue(180);
    overlayAlphaSlider_->setMinimumWidth(120);
    overlayAlphaValueLabel_ = new QLabel(QString::number(overlayAlphaSlider_->value()), this);
    overlayAlphaValueLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    overlayAlphaValueLabel_->setMinimumWidth(28);

    controlsLayout->addWidget(new QLabel(tr("Luma Min"), this), 0, 0);
    controlsLayout->addWidget(lumaMinSpinBox_, 0, 1);
    controlsLayout->addWidget(new QLabel(tr("Luma Max"), this), 0, 2);
    controlsLayout->addWidget(lumaMaxSpinBox_, 0, 3);
    controlsLayout->addWidget(new QLabel(tr("Chroma Min"), this), 0, 4);
    controlsLayout->addWidget(chromaMinSpinBox_, 0, 5);
    controlsLayout->addWidget(new QLabel(tr("Chroma Max"), this), 0, 6);
    controlsLayout->addWidget(chromaMaxSpinBox_, 0, 7);
    controlsLayout->addWidget(overlayButton_, 1, 0, 1, 2);
    controlsLayout->addWidget(new QLabel(tr("Overlay Alpha"), this), 1, 2);
    controlsLayout->addWidget(overlayAlphaSlider_, 1, 3, 1, 4);
    controlsLayout->addWidget(overlayAlphaValueLabel_, 1, 7);
    controlsLayout->setColumnStretch(8, 1);
    mainLayout->addLayout(controlsLayout);

    connect(lumaMinSpinBox_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (lumaMaxSpinBox_ && value > lumaMaxSpinBox_->value()) {
            setSpinBoxValueWithoutSignals(lumaMaxSpinBox_, value);
        }
        emitSettingsChanged();
    });
    connect(lumaMaxSpinBox_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (lumaMinSpinBox_ && value < lumaMinSpinBox_->value()) {
            setSpinBoxValueWithoutSignals(lumaMinSpinBox_, value);
        }
        emitSettingsChanged();
    });
    connect(chromaMinSpinBox_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (chromaMaxSpinBox_ && value > chromaMaxSpinBox_->value()) {
            setSpinBoxValueWithoutSignals(chromaMaxSpinBox_, value);
        }
        emitSettingsChanged();
    });
    connect(chromaMaxSpinBox_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (chromaMinSpinBox_ && value < chromaMinSpinBox_->value()) {
            setSpinBoxValueWithoutSignals(chromaMinSpinBox_, value);
        }
        emitSettingsChanged();
    });
    connect(overlayButton_, &QPushButton::toggled, this, [this](bool checked) {
        overlayButton_->setText(checked ? tr("Overlay On") : tr("Overlay Off"));
        emitSettingsChanged();
    });
    connect(overlayAlphaSlider_, &QSlider::valueChanged, this, [this](int value) {
        if (overlayAlphaValueLabel_) {
            overlayAlphaValueLabel_->setText(QString::number(value));
        }
        emitSettingsChanged();
    });
}

void YuvRangeDialog::showScopeImage(const QImage &scopeImage)
{
    cachedScopeImage_ = scopeImage;
    updateScopeLabelPixmap();
}

QSize YuvRangeDialog::scopeRenderTargetSize() const
{
    if (scopeLabel_) {
        return scopeLabel_->size().expandedTo(QSize(1, 1));
    }
    return size().expandedTo(QSize(1, 1));
}

YuvRangeSettings YuvRangeDialog::settings() const
{
    YuvRangeSettings currentSettings;
    currentSettings.lumaMin = lumaMinSpinBox_ ? lumaMinSpinBox_->value() : currentSettings.lumaMin;
    currentSettings.lumaMax = lumaMaxSpinBox_ ? lumaMaxSpinBox_->value() : currentSettings.lumaMax;
    currentSettings.chromaMin = chromaMinSpinBox_ ? chromaMinSpinBox_->value() : currentSettings.chromaMin;
    currentSettings.chromaMax = chromaMaxSpinBox_ ? chromaMaxSpinBox_->value() : currentSettings.chromaMax;
    currentSettings.overlayAlpha = overlayAlphaSlider_ ? overlayAlphaSlider_->value() : currentSettings.overlayAlpha;
    currentSettings.overlayEnabled = overlayButton_ ? overlayButton_->isChecked() : currentSettings.overlayEnabled;
    return currentSettings;
}

void YuvRangeDialog::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    updateScopeLabelPixmap();
    emit renderTargetSizeChanged(scopeRenderTargetSize());
}

void YuvRangeDialog::setSpinBoxValueWithoutSignals(QSpinBox *spinBox, qint32 value)
{
    if (!spinBox) {
        return;
    }

    const QSignalBlocker blocker(spinBox);
    spinBox->setValue(value);
}

void YuvRangeDialog::emitSettingsChanged()
{
    emit settingsChanged(settings());
}

void YuvRangeDialog::updateScopeLabelPixmap()
{
    if (!scopeLabel_) {
        return;
    }

    if (cachedScopeImage_.isNull()) {
        scopeLabel_->setPixmap(QPixmap());
        scopeLabel_->setText(tr("No YUV range data"));
        return;
    }

    const QSize targetSize = scopeLabel_->size().expandedTo(QSize(1, 1));
    const QPixmap pixmap = QPixmap::fromImage(cachedScopeImage_).scaled(
        targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    scopeLabel_->setText(QString());
    scopeLabel_->setPixmap(pixmap);
}
