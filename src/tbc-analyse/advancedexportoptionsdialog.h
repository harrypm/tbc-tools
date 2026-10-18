/******************************************************************************
 * advancedexportoptionsdialog.h
 * tbc-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Harry Munday
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#ifndef ADVANCEDEXPORTOPTIONSDIALOG_H
#define ADVANCEDEXPORTOPTIONSDIALOG_H

#include <QDialog>

#include "configuration.h"  // for ExportAdvancedOptions

class QCheckBox;
class QLabel;
class QSpinBox;

class AdvancedExportOptionsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AdvancedExportOptionsDialog(const ExportAdvancedOptions &current,
                                         bool letterboxCropAllowed,
                                         QWidget *parent = nullptr);
    ~AdvancedExportOptionsDialog() override;

    ExportAdvancedOptions selectedOptions() const;

private slots:
    void onLetterboxToggled(bool checked);
    void onForceAnamorphicToggled(bool checked);
    void onReverseFieldOrderToggled(bool checked);

private:
    ExportAdvancedOptions currentOptions;  // initial values shown when the dialog opens
    bool letterboxCropAllowed = false;

    QCheckBox *exportMetadataCheckBox = nullptr;
    QCheckBox *disableMetadataEmbeddingCheckBox = nullptr;
    QCheckBox *letterboxCropCheckBox = nullptr;
    QCheckBox *forceAnamorphicCheckBox = nullptr;
    QCheckBox *reverseFieldOrderCheckBox = nullptr;
    QCheckBox *twoStepCheckBox = nullptr;
    QSpinBox *outputPaddingSpinBox = nullptr;
    QSpinBox *threadsSpinBox = nullptr;
    QLabel *reverseNoteLabel = nullptr;
};

#endif // ADVANCEDEXPORTOPTIONSDIALOG_H
