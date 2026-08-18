/******************************************************************************
 * rgbscopedialog.cpp
 * tbc-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#include "rgbscopedialog.h"

#include <QLabel>
#include <QPixmap>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QVBoxLayout>

RgbScopeDialog::RgbScopeDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("RGB Scope"));
    setWindowFlags(Qt::Window);
    setModal(false);
    resize(960, 540);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 6, 6, 6);
    mainLayout->setSpacing(0);

    scopeLabel_ = new QLabel(this);
    scopeLabel_->setAlignment(Qt::AlignCenter);
    scopeLabel_->setMinimumSize(1, 1);
    scopeLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    scopeLabel_->setScaledContents(false);
    scopeLabel_->setText(tr("No RGB scope data"));
    mainLayout->addWidget(scopeLabel_);
}

void RgbScopeDialog::showScopeImage(const QImage &scopeImage)
{
    cachedScopeImage_ = scopeImage;
    updateScopeLabelPixmap();
}

QSize RgbScopeDialog::scopeRenderTargetSize() const
{
    if (scopeLabel_) {
        return scopeLabel_->size().expandedTo(QSize(1, 1));
    }
    return size().expandedTo(QSize(1, 1));
}

void RgbScopeDialog::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    updateScopeLabelPixmap();
    emit renderTargetSizeChanged(scopeRenderTargetSize());
}

void RgbScopeDialog::updateScopeLabelPixmap()
{
    if (!scopeLabel_) {
        return;
    }

    if (cachedScopeImage_.isNull()) {
        scopeLabel_->setPixmap(QPixmap());
        scopeLabel_->setText(tr("No RGB scope data"));
        return;
    }

    const QSize targetSize = scopeLabel_->size().expandedTo(QSize(1, 1));
    const QPixmap pixmap = QPixmap::fromImage(cachedScopeImage_).scaled(
        targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    scopeLabel_->setText(QString());
    scopeLabel_->setPixmap(pixmap);
}
