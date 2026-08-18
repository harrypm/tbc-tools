/******************************************************************************
 * aboutdialog.cpp
 * tbc-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018-2025 Simon Inns
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#include "aboutdialog.h"
#include "ui_aboutdialog.h"

AboutDialog::AboutDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AboutDialog)
{
    ui->setupUi(this);
    ui->gitVersionLabel->setText(QString("TBC-Tools · Version %1").arg(APP_VERSION));
    ui->gitVersionLabel->setToolTip(QString("Build metadata: %1 / %2").arg(APP_BRANCH, APP_COMMIT));
}

AboutDialog::~AboutDialog()
{
    delete ui;
}
