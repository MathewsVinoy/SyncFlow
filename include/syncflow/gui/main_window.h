#pragma once

#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QString>

namespace syncflow {
namespace gui {

class SettingsDialog;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:

    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onSettingsClicked();
    void onReloadClicked();

private:
    void setupUI();
    void createMenuBar();
    void connectSignals();
    void loadSettings();
    void saveSettings();
    void refreshConfigView();
    void launchBackgroundPeer();
    QString findPeerExecutablePath() const;

    struct ConfigView {
        bool file_sync_enabled{true};
        bool security_enabled{true};
        bool require_approval{true};
        QString device_name;
        QString source_path;
        QString receive_dir;
    };

    // UI Components
    QLabel* config_path_label_;
    QLabel* device_name_label_;
    QLabel* file_sync_label_;
    QLabel* source_path_label_;
    QLabel* receive_dir_label_;
    QLabel* security_label_;
    QLabel* approval_label_;
    QLabel* background_status_label_;
    QPushButton* settings_button_;
    QPushButton* reload_button_;
    
    // State
    QString config_path_;
    ConfigView config_;
    QString device_name_;
    QString local_ip_;
};

} // namespace gui
} // namespace syncflow
