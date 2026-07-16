#include <QApplication>
#include <QDialog>
#include <QFile>
#include <QFont>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QSplashScreen>
#include <QTimer>
#include "main_window.h"
#include "ui/login_dialog.h"

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  app.setApplicationName("goods_db_studio");
  app.setApplicationVersion("1.0.0");
  app.setOrganizationName("goods_db");

  // ---- Splash Screen ----
  // Create a programmatic splash pixmap (256x256)
  QPixmap pixmap(400, 300);
  pixmap.fill(QColor("#1e1e1e"));

  {
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    // Draw database icon (simplified cylinder)
    painter.setPen(QPen(QColor("#0078d4"), 3));
    painter.setBrush(Qt::NoBrush);

    // Cylinder top ellipse
    QRectF top_rect(125, 40, 150, 40);
    painter.drawEllipse(top_rect);

    // Cylinder body
    painter.drawLine(QPointF(125, 60), QPointF(125, 160));
    painter.drawLine(QPointF(275, 60), QPointF(275, 160));

    // Cylinder bottom arc (partial ellipse)
    QRectF bottom_rect(125, 140, 150, 40);
    painter.drawArc(QRectF(125, 140, 150, 40), 0, 180 * 16);
    painter.drawArc(QRectF(125, 140, 150, 40), 180 * 16, 180 * 16);

    // Brand name
    QFont title_font("sans-serif", 32, QFont::Bold);
    painter.setFont(title_font);
    painter.setPen(QColor("#ffffff"));
    painter.drawText(QRectF(0, 200, 400, 50), Qt::AlignCenter, "goods_db");

    // Subtitle
    QFont sub_font("sans-serif", 12);
    painter.setFont(sub_font);
    painter.setPen(QColor("#888888"));
    painter.drawText(QRectF(0, 245, 400, 30), Qt::AlignCenter, "New-Retail Database System");

    // Version
    QFont ver_font("sans-serif", 9);
    painter.setFont(ver_font);
    painter.setPen(QColor("#666666"));
    painter.drawText(QRectF(0, 270, 400, 20), Qt::AlignCenter, "v1.0.0");
  }

  QSplashScreen splash(pixmap);
  splash.show();
  splash.showMessage("Loading goods_db_studio v1.0.0...",
                     Qt::AlignBottom | Qt::AlignCenter, Qt::white);
  app.processEvents();

  // Load dark theme by default
  QFile themeFile(":/themes/dark.qss");
  if (themeFile.open(QFile::ReadOnly | QFile::Text)) {
    QString styleSheet = themeFile.readAll();
    app.setStyleSheet(styleSheet);
    themeFile.close();
  }

  splash.showMessage("Initializing modules...",
                     Qt::AlignBottom | Qt::AlignCenter, Qt::white);
  app.processEvents();

  // Brief delay to show splash
  QTimer::singleShot(1500, &splash, &QWidget::close);

  // ---- Show login dialog ----
  goods_db::studio::LoginDialog login_dialog;
  if (login_dialog.exec() != QDialog::Accepted) {
    return 0;  // User closed the dialog — exit
  }

  auto login_info = login_dialog.GetLoginInfo();

  goods_db::studio::MainWindow window;
  window.SetInitialConnection(login_info);
  splash.finish(&window);
  window.show();

  return app.exec();
}
