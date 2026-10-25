#pragma once
class QAction;
class QWidget;

// Shows a modal dialog. The optional action belongs to the host.
void showAboutDialog(QWidget *parent, QAction *donationAction = nullptr);
