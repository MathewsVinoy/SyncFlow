#pragma once

#include <QMainWindow>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QThread>
#include <QTimer>
#include <memory>

namespace syncflow {
namespace gui {

class SyncWorker;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:

    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onDeviceDiscovered(const QString& device_name, const QString& device_ip, const QString& fingerprint);
    void onDeviceConnected(const QString& device_name);
    void onDeviceDisconnected(const QString& device_name);
    void onSyncStarted();
    void onSyncProgress(int progress);
    void onSyncCompleted();
    void onSyncError(const QString& error_message);
    void onSettingsClicked();
    void onApproveClicked();
    void onRemoveClicked();
    void onPeriodicCheck();  // Called every 10 seconds to check SSL and connections

private:
    void setupUI();
    void createMenuBar();
    void connectSignals();
    void loadSettings();
    void saveSettings();
    void updateDeviceList();
    void startSyncWorker();
    void checkSSLCertificates();

    // UI Components
    QListWidget* device_list_widget_;
    QLabel* status_label_;
    QLabel* device_name_label_;
    QLabel* ip_address_label_;
    QLabel* sync_status_label_;
    QPushButton* settings_button_;
    QPushButton* approve_button_;
    QPushButton* remove_button_;

    // Backend
    std::unique_ptr<SyncWorker> sync_worker_;
    QThread* worker_thread_;
    QTimer* periodic_timer_;  // 10-second timer for SSL and connection checks
    
    // State
    QString device_name_;
    QString local_ip_;
};

} // namespace gui
} // namespace syncflow
