#pragma once

#include <QMainWindow>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QThread>
#include <memory>

namespace syncflow::gui {

class SyncWorker;

class SyncflowMainWindow : public QMainWindow {
    Q_OBJECT

public:

    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onDeviceDiscovered(const QString& device_name, const QString& device_ip);
    void onDeviceConnected(const QString& device_name);
    void onDeviceDisconnected(const QString& device_name);
    void onSyncStarted(const QString& device_name);
    void onSyncProgress(const QString& file_name, int progress);
    void onSyncCompleted(const QString& device_name);
    void onSyncError(const QString& error_message);
    void onSettingsClicked();
    void onApproveClicked();
    void onRemoveClicked();
    void onStartStopClicked();
    void onRefreshClicked();

private:
    void setupUI();
    void createMenuBar();
    void connectSignals();
    void loadSettings();
    void saveSettings();
    void updateDeviceList();
    void startSyncWorker();
    void stopSyncWorker();

    // UI Components
    QListWidget* device_list_widget_;
    QLabel* status_label_;
    QLabel* device_name_label_;
    QLabel* ip_address_label_;
    QLabel* current_sync_label_;
    QProgressBar* sync_progress_;
    QPushButton* start_stop_button_;
    QPushButton* settings_button_;
    QPushButton* approve_button_;
    QPushButton* remove_button_;
    QPushButton* refresh_button_;

    // Backend
    std::unique_ptr<SyncWorker> sync_worker_;
    QThread* worker_thread_;
    
    // State
    bool is_running_;
    QString device_name_;
    QString local_ip_;
};

}  // namespace syncflow::gui
