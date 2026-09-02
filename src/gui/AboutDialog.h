#pragma once

#include <QDialog>
#include <QWidget>

#include "gui/qtgettext.h"
#include "ui_AboutDialog.h"

// Deliberately does not include version.h: this header is processed by moc, and
// anything it includes ends up in the generated mocs_compilation.cpp too. That
// file cannot be listed in OPENSCAD_VERSION_SOURCES, so the version definitions
// would have to be global again - and then every commit would invalidate every
// object in the build. The constructor is defined in AboutDialog.cc instead.
class AboutDialog : public QDialog, public Ui::AboutDialog
{
  Q_OBJECT;

public:
  AboutDialog(QWidget *);

public slots:
  void on_okPushButton_clicked() { accept(); }
};
