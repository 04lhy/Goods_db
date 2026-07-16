#include "ui/welcome_widget.h"

#include <QApplication>
#include <QFile>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QScrollArea>
#include <QSettings>
#include <QStyle>

#include "connection/connection_pool.h"

namespace goods_db {
namespace studio {

WelcomeWidget::WelcomeWidget(ConnectionPool* pool, QWidget* parent)
    : QWidget(parent), pool_(pool) {
  SetupUi();
}

void WelcomeWidget::SetupUi() {
  auto* outer_layout = new QVBoxLayout(this);
  outer_layout->setContentsMargins(0, 0, 0, 0);

  // Scroll area for the welcome content
  auto* scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);

  auto* content = new QWidget(scroll);
  auto* layout = new QVBoxLayout(content);
  layout->setContentsMargins(48, 48, 48, 48);
  layout->setSpacing(32);
  layout->setAlignment(Qt::AlignTop | Qt::AlignHCenter);

  // ---- Logo / Branding ----
  auto* logo_label = new QLabel("goods_db", content);
  QFont logo_font = logo_label->font();
  logo_font.setPointSize(36);
  logo_font.setBold(true);
  logo_label->setFont(logo_font);
  logo_label->setAlignment(Qt::AlignCenter);
  logo_label->setStyleSheet("color: #0078d4;");
  layout->addWidget(logo_label);

  auto* tagline = new QLabel(
      tr("Lightweight New-Retail Database Management System"), content);
  QFont tag_font = tagline->font();
  tag_font.setPointSize(13);
  tagline->setFont(tag_font);
  tagline->setAlignment(Qt::AlignCenter);
  tagline->setStyleSheet("color: #888; margin-bottom: 8px;");
  layout->addWidget(tagline);

  // ---- Separator ----
  auto* sep1 = new QFrame(content);
  sep1->setFrameShape(QFrame::HLine);
  sep1->setStyleSheet("QFrame { color: #444; }");
  layout->addWidget(sep1);

  // ---- Quick Actions ----
  layout->addWidget(CreateQuickActions());

  // ---- Separator ----
  auto* sep2 = new QFrame(content);
  sep2->setFrameShape(QFrame::HLine);
  sep2->setStyleSheet("QFrame { color: #444; }");
  layout->addWidget(sep2);

  // ---- Recent Connections ----
  layout->addWidget(CreateRecentConnections());

  // ---- Spacer ----
  layout->addStretch(1);

  // ---- Version Info ----
  layout->addWidget(CreateVersionInfo());

  scroll->setWidget(content);
  outer_layout->addWidget(scroll);
}

QWidget* WelcomeWidget::CreateQuickActions() {
  auto* widget = new QWidget(this);
  auto* layout = new QVBoxLayout(widget);
  layout->setSpacing(12);

  auto* title = new QLabel(tr("Quick Start"), widget);
  QFont title_font = title->font();
  title_font.setPointSize(14);
  title_font.setBold(true);
  title->setFont(title_font);
  layout->addWidget(title);

  auto* btn_layout = new QHBoxLayout();
  btn_layout->setSpacing(16);
  btn_layout->setAlignment(Qt::AlignCenter);

  auto makeActionBtn = [this, widget](const QString& text, const QString& icon_path,
                                       const QString& desc) {
    auto* btn = new QPushButton(widget);
    btn->setIcon(QIcon(icon_path));
    btn->setIconSize(QSize(24, 24));
    btn->setText(text);
    btn->setMinimumSize(200, 64);
    btn->setToolTip(desc);
    btn->setStyleSheet(
        "QPushButton {"
        "  font-size: 13px; font-weight: bold; padding: 12px 20px;"
        "  border-radius: 6px;"
        "}");
    return btn;
  };

  auto* new_conn_btn = makeActionBtn(tr("New Connection"), ":/icons/new_connection.svg",
                                     tr("Connect to a goods_db server"));
  connect(new_conn_btn, &QPushButton::clicked, this, &WelcomeWidget::NewConnectionRequested);
  btn_layout->addWidget(new_conn_btn);

  auto* open_file_btn = makeActionBtn(tr("Open SQL File"), ":/icons/open_file.svg",
                                      tr("Open and edit a .sql file"));
  connect(open_file_btn, &QPushButton::clicked, this, &WelcomeWidget::OpenFileRequested);
  btn_layout->addWidget(open_file_btn);

  layout->addLayout(btn_layout);
  return widget;
}

QWidget* WelcomeWidget::CreateRecentConnections() {
  auto* widget = new QWidget(this);
  auto* layout = new QVBoxLayout(widget);
  layout->setSpacing(12);

  auto* title = new QLabel(tr("Recent Connections"), widget);
  QFont title_font = title->font();
  title_font.setPointSize(14);
  title_font.setBold(true);
  title->setFont(title_font);
  layout->addWidget(title);

  // Container for recent connection items
  auto* list_widget = new QWidget(widget);
  recent_list_layout_ = new QVBoxLayout(list_widget);
  recent_list_layout_->setSpacing(4);
  recent_list_layout_->setContentsMargins(0, 0, 0, 0);

  // Load from settings
  QSettings settings("goods_db", "goods_db_studio");
  int size = settings.beginReadArray("recent_connections");
  if (size == 0) {
    auto* empty_label = new QLabel(
        tr("No recent connections. Click \"New Connection\" to get started."), widget);
    empty_label->setAlignment(Qt::AlignCenter);
    empty_label->setStyleSheet("color: #777; padding: 16px;");
    recent_list_layout_->addWidget(empty_label);
  } else {
    for (int i = 0; i < size && i < 5; i++) {
      settings.setArrayIndex(i);
      QString name = settings.value("name").toString();
      QString host = settings.value("host").toString();
      QString port = settings.value("port").toString();

      auto* btn = new QPushButton(
          QString("%1  (%2:%3)").arg(name, host, port), widget);
      btn->setFlat(true);
      btn->setCursor(Qt::PointingHandCursor);
      btn->setStyleSheet(
          "QPushButton { text-align: left; padding: 8px 12px; border-radius: 4px; }"
          "QPushButton:hover { background-color: rgba(0,120,212,0.2); }");
      connect(btn, &QPushButton::clicked, this, [this, name]() {
        emit RecentConnectionClicked(name);
      });
      recent_list_layout_->addWidget(btn);
    }
  }
  settings.endArray();

  layout->addWidget(list_widget);
  return widget;
}

QWidget* WelcomeWidget::CreateVersionInfo() {
  auto* widget = new QWidget(this);
  auto* layout = new QVBoxLayout(widget);
  layout->setAlignment(Qt::AlignCenter);
  layout->setSpacing(4);

  auto* ver_label = new QLabel(
      QString("goods_db_studio v%1").arg(QApplication::applicationVersion()), widget);
  ver_label->setAlignment(Qt::AlignCenter);
  ver_label->setStyleSheet("color: #666; font-size: 11px;");
  layout->addWidget(ver_label);

  auto* copyright = new QLabel(
      tr("Database System Design Course Project — Hunan University"), widget);
  copyright->setAlignment(Qt::AlignCenter);
  copyright->setStyleSheet("color: #555; font-size: 10px;");
  layout->addWidget(copyright);

  return widget;
}

}  // namespace studio
}  // namespace goods_db
