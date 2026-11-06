/*
SPDX-FileCopyrightText: 2026 Kdenlive contributors
SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "cleanupdialog.hpp"

#include <KLocalizedString>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>

QVector<QPair<double, double>> StoryCleanup::choose(QWidget *parent, const QString &title, const QString &help, QVector<StoryCleanupChoice> choices,
                                                    bool *duplicate)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    auto *layout = new QVBoxLayout(&dialog);
    auto *helpLabel = new QLabel(help, &dialog);
    helpLabel->setWordWrap(true);
    layout->addWidget(helpLabel);
    auto *list = new QListWidget(&dialog);
    for (const StoryCleanupChoice &choice : choices) {
        auto *item = new QListWidgetItem(choice.note.isEmpty() ? choice.label : QStringLiteral("%1 — %2").arg(choice.label, choice.note), list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(choice.checked ? Qt::Checked : Qt::Unchecked);
    }
    layout->addWidget(list);
    auto *duplicateBox = new QCheckBox(i18n("Duplicate the sequence before applying"), &dialog);
    duplicateBox->setChecked(true);
    layout->addWidget(duplicateBox);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return {};
    }
    if (duplicate != nullptr) {
        *duplicate = duplicateBox->isChecked();
    }
    QVector<QPair<double, double>> picked;
    for (int i = 0; i < choices.size() && i < list->count(); ++i) {
        if (list->item(i)->checkState() == Qt::Checked) {
            picked.push_back({choices.at(i).startSec, choices.at(i).endSec});
        }
    }
    return picked;
}
