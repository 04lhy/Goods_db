#include <QApplication>
#include <QFile>
#include "main_window.h"

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  app.setApplicationName("goods_db_studio");
  app.setApplicationVersion("0.1.0");
  app.setOrganizationName("goods_db");

  // Load dark theme by default
  QFile themeFile(":/themes/dark.qss");
  if (themeFile.open(QFile::ReadOnly | QFile::Text)) {
    QString styleSheet = themeFile.readAll();
    app.setStyleSheet(styleSheet);
    themeFile.close();
  }

  goods_db::studio::MainWindow window;
  window.show();

  return app.exec();
}
