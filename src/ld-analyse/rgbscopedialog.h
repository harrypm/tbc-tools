/******************************************************************************
 * rgbscopedialog.h
 * tbc-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#ifndef RGBSCOPEDIALOG_H
#define RGBSCOPEDIALOG_H

#include <QDialog>
#include <QImage>
#include <QSize>

class QLabel;
class QResizeEvent;

class RgbScopeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RgbScopeDialog(QWidget *parent = nullptr);
    ~RgbScopeDialog() override = default;

    void showScopeImage(const QImage &scopeImage);
    QSize scopeRenderTargetSize() const;

signals:
    void renderTargetSizeChanged(const QSize &targetSize);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateScopeLabelPixmap();

    QLabel *scopeLabel_ = nullptr;
    QImage cachedScopeImage_;
};

#endif // RGBSCOPEDIALOG_H
