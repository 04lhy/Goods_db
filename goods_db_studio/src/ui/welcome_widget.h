#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace goods_db {
namespace studio {

class ConnectionPool;

// =============================================================================
// WelcomeWidget — home screen shown in the central tab area
//
// Displays:
//   - goods_db logo / branding
//   - Quick actions: New Connection, Open SQL File
//   - Recent connections list
//   - Version info
// =============================================================================
class WelcomeWidget : public QWidget {
  Q_OBJECT

 public:
  explicit WelcomeWidget(ConnectionPool* pool, QWidget* parent = nullptr);

 signals:
  void NewConnectionRequested();
  void OpenFileRequested();
  void RecentConnectionClicked(const QString& name);

 private:
  void SetupUi();
  QWidget* CreateQuickActions();
  QWidget* CreateRecentConnections();
  QWidget* CreateVersionInfo();

  ConnectionPool* pool_;
  QVBoxLayout* recent_list_layout_;
};

}  // namespace studio
}  // namespace goods_db
