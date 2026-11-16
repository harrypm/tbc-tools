/******************************************************************************
 * vbiprocessingdialog.h
 * tbc-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Harry Munday
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#ifndef VBIPROCESSINGDIALOG_H
#define VBIPROCESSINGDIALOG_H

#include <QDialog>

#include "configuration.h"  // for VbiProcessingOptions

class QCheckBox;
class QGroupBox;
class QLineEdit;
class QSpinBox;

class VbiProcessingDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VbiProcessingDialog(const VbiProcessingOptions &current, QWidget *parent = nullptr);
    ~VbiProcessingDialog() override;

    VbiProcessingOptions selectedOptions() const;

private slots:
    void onTeletextToggled(bool checked);
    void onBrowseTeletextDir();

private:
    VbiProcessingOptions currentOptions;  // initial values shown when the dialog opens

    QCheckBox *vbiCoreCheckBox = nullptr;
    QCheckBox *ntscCheckBox = nullptr;
    QCheckBox *vitcCheckBox = nullptr;
    QCheckBox *closedCaptionsCheckBox = nullptr;
    QCheckBox *teletextCheckBox = nullptr;
    QCheckBox *vitsCheckBox = nullptr;

    QGroupBox *teletextGroupBox = nullptr;
    QLineEdit *teletextHtmlDirLineEdit = nullptr;
    QLineEdit *teletextTapeFormatLineEdit = nullptr;
    QSpinBox *teletextMinDuplicatesSpinBox = nullptr;
};

#endif // VBIPROCESSINGDIALOG_H
