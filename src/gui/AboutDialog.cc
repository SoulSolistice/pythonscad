#include "gui/AboutDialog.h"

#include <QFile>
#include <QIODevice>
#include <QString>
#include <QWidget>
#include <string>

#include "gui/UIUtils.h"
#include "gui/qtgettext.h"
#include "version.h"

// The constructor lives here rather than in the header because it is the only
// thing in the dialog which needs version.h, and the header is processed by moc.
// A header that reaches moc drags version.h into the generated
// mocs_compilation.cpp, which is not a file the build can name in
// OPENSCAD_VERSION_SOURCES - so the version definitions would have to go back to
// being global, and a single commit would once again invalidate every object in
// the build. See the note beside OPENSCAD_VERSION_SOURCES in CMakeLists.txt.
AboutDialog::AboutDialog(QWidget *)
{
  setupUi(this);
  this->setWindowTitle(QString(_("About PythonSCAD")) + " " +
                       QString::fromStdString(std::string(openscad_shortversionnumber)));

  QString titleText = this->titleLabel->text();
  titleText.replace("__PYTHON_BRAND_COLOR__", UIUtils::pythonBrandColor);
  this->titleLabel->setText(titleText);

  QFile htmlFile(":/html/AboutDialog.html");
  if (htmlFile.open(QIODevice::ReadOnly)) {
    QString tmp = QString::fromUtf8(htmlFile.readAll());
    tmp.replace("__VERSION__", QString::fromStdString(std::string(openscad_detailedversionnumber)));
    tmp.replace("__PYTHON_BRAND_COLOR__", UIUtils::pythonBrandColor);
    this->aboutText->setHtml(tmp);
  }
}
