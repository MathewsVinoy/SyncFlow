#include "syncflow/gui/main_window.h"
#include "syncflow/gui/settings_dialog.h"
#include "syncflow/gui/device_approval_dialog.h"
#include "syncflow/gui/sync_worker.h"
#include "syncflow/platform/system_info.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QSettings>
#include <QClipboard>
#include <QApplication>
#include <QMessageBox>
#include <QThread>
#include <QListWidgetItem>
#include <QSplitter>
#include <QTimer>
#include <QDir>
#include <QFile>

namespace syncflow::gui {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      worker_thread_(nullptr),
      periodic_timer_(nullptr) {
    
    setWindowTitle("Syncflow - Peer to Peer File Sync");
    setWindowIcon(QIcon(":/icons/syncflow.png"));
    setGeometry(100, 100, 1000, 600);

    device_name_ = QString::fromStdString(syncflow::platform::get_hostname());
    local_ip_ = QString::fromStdString(syncflow::platform::get_local_ipv4());

    setupUI();
    createMenuBar();
    loadSettings();

    // Create sync worker thread
    sync_worker_ = std::make_unique<SyncWorker>();
    worker_thread_ = new QThread(this);
    sync_worker_->moveToThread(worker_thread_);

    connect(worker_thread_, &QThread::finished, sync_worker_.get(), &QObject::deleteLater);
    connect(this, &MainWindow::destroyed, worker_thread_, &QThread::quit);

    connectSignals();

    // Auto-start the sync worker
    startSyncWorker();

    // Setup 10-second periodic timer for SSL/device checks
    periodic_timer_ = new QTimer(this);
    connect(periodic_timer_, &QTimer::timeout, this, &MainWindow::onPeriodicCheck);
    periodic_timer_->start(10000);  // 10 seconds
}

MainWindow::~MainWindow() {
    if (periodic_timer_) {
        periodic_timer_->stop();
    }
    if (worker_thread_) {
        worker_thread_->quit();
        worker_thread_->wait();
    }
}

void MainWindow::setupUI() {
    QWidget* central_widget = new QWidget(this);
    setCentralWidget(central_widget);

    QVBoxLayout* main_layout = new QVBoxLayout(central_widget);

    // Header with device info
    QGroupBox* header_group = new QGroupBox("Device Information", this);
    QVBoxLayout* header_layout = new QVBoxLayout(header_group);

    device_name_label_ = new QLabel(QString("Device: %1").arg(device_name_), this);
    ip_address_label_ = new QLabel(QString("IP Address: %1").arg(local_ip_), this);
    status_label_ = new QLabel("Status: Discovering devices...", this);

    header_layout->addWidget(device_name_label_);
    header_layout->addWidget(ip_address_label_);
    header_layout->addWidget(status_label_);

    main_layout->addWidget(header_group);

    // Device list
    QGroupBox* devices_group = new QGroupBox("Connected Devices", this);
    QVBoxLayout* devices_layout = new QVBoxLayout(devices_group);

    device_list_widget_ = new QListWidget(this);
    device_list_widget_->setMinimumHeight(300);
    devices_layout->addWidget(device_list_widget_);

    main_layout->addWidget(devices_group);

    // Sync status (simplified, no progress bar)
    QGroupBox* sync_group = new QGroupBox("Sync Status", this);
    QVBoxLayout* sync_layout = new QVBoxLayout(sync_group);

    sync_status_label_ = new QLabel("Syncing in background...", this);
    sync_status_label_->setStyleSheet("color: green; font-weight: bold;");

    sync_layout->addWidget(sync_status_label_);

    main_layout->addWidget(sync_group);

    // Control buttons (simplified: removed Start/Stop button)
    QHBoxLayout* button_layout = new QHBoxLayout();

    approve_button_ = new QPushButton("Approve Device", this);
    approve_button_->setMinimumWidth(120);

    remove_button_ = new QPushButton("Remove Device", this);
    remove_button_->setMinimumWidth(120);

    settings_button_ = new QPushButton("Settings", this);
    settings_button_->setMinimumWidth(120);

    button_layout->addWidget(approve_button_);
    button_layout->addWidget(remove_button_);
    button_layout->addWidget(settings_button_);
    button_layout->addStretch();

    main_layout->addLayout(button_layout);

    central_widget->setLayout(main_layout);
}

void MainWindow::createMenuBar() {
    QMenuBar* menu_bar = new QMenuBar(this);
    setMenuBar(menu_bar);

    // File menu
    QMenu* file_menu = menu_bar->addMenu("&File");
    
    QAction* settings_action = file_menu->addAction("&Settings");
    connect(settings_action, &QAction::triggered, this, &MainWindow::onSettingsClicked);

    file_menu->addSeparator();

    QAction* exit_action = file_menu->addAction("E&xit");
    connect(exit_action, &QAction::triggered, this, &QWidget::close);

    // Help menu
    QMenu* help_menu = menu_bar->addMenu("&Help");
    
    QAction* about_action = help_menu->addAction("&About");
    connect(about_action, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, "About Syncflow",
            "Syncflow v1.0\n\nPeer-to-peer file synchronization system.\n\n"
            "Supports cross-platform synchronization across Linux, macOS, Windows, and Android.");
    });
}

void MainWindow::connectSignals() {
    if (!sync_worker_) return;

    // Use Qt::QueuedConnection for thread-safe signal delivery from worker thread
    connect(sync_worker_.get(), &SyncWorker::deviceDiscovered,
            this, &MainWindow::onDeviceDiscovered, Qt::QueuedConnection);
    connect(sync_worker_.get(), &SyncWorker::deviceConnected,
            this, &MainWindow::onDeviceConnected, Qt::QueuedConnection);
    connect(sync_worker_.get(), &SyncWorker::deviceDisconnected,
            this, &MainWindow::onDeviceDisconnected, Qt::QueuedConnection);
    connect(sync_worker_.get(), &SyncWorker::syncStarted,
            this, &MainWindow::onSyncStarted, Qt::QueuedConnection);
    connect(sync_worker_.get(), &SyncWorker::syncProgress,
            this, &MainWindow::onSyncProgress, Qt::QueuedConnection);
    connect(sync_worker_.get(), &SyncWorker::syncCompleted,
            this, &MainWindow::onSyncCompleted, Qt::QueuedConnection);
    connect(sync_worker_.get(), &SyncWorker::syncError,
            this, &MainWindow::onSyncError, Qt::QueuedConnection);
    connect(sync_worker_.get(), &SyncWorker::statusChanged,
            this, [this](const QString& status) {
                // Update status label with worker status updates
                if (!status.isEmpty()) {
                    // Can be used for debugging or showing worker status
                }
            }, Qt::QueuedConnection);

    connect(settings_button_, &QPushButton::clicked,
            this, &MainWindow::onSettingsClicked);
    connect(approve_button_, &QPushButton::clicked,
            this, &MainWindow::onApproveClicked);
    connect(remove_button_, &QPushButton::clicked,
            this, &MainWindow::onRemoveClicked);
}

void MainWindow::onDeviceDiscovered(const QString& device_name, const QString& device_ip, const QString& fingerprint) {
    // Avoid duplicates: update existing item if already present
    for (int i = 0; i < device_list_widget_->count(); ++i) {
        QListWidgetItem* existing = device_list_widget_->item(i);
        if (existing->data(Qt::UserRole).toString() == device_name) {
            existing->setText(QString("%1 (%2)").arg(device_name, device_ip));
            existing->setData(Qt::UserRole + 1, fingerprint);
            return;
        }
    }

    QListWidgetItem* item = new QListWidgetItem(
        QString("%1 (%2)").arg(device_name, device_ip), device_list_widget_);
    item->setData(Qt::UserRole, device_name);
    item->setData(Qt::UserRole + 1, fingerprint);
    device_list_widget_->addItem(item);

    status_label_->setText(QString("Status: Device discovered (%1)").arg(device_name));
}

void MainWindow::onDeviceConnected(const QString& device_name) {
    for (int i = 0; i < device_list_widget_->count(); ++i) {
        QListWidgetItem* item = device_list_widget_->item(i);
        if (item->data(Qt::UserRole).toString() == device_name) {
            item->setBackground(QColor(144, 238, 144));  // Light green
            status_label_->setText(QString("Status: Connected to %1").arg(device_name));
            break;
        }
    }
}

void MainWindow::onDeviceDisconnected(const QString& device_name) {
    for (int i = 0; i < device_list_widget_->count(); ++i) {
        QListWidgetItem* item = device_list_widget_->item(i);
        if (item->data(Qt::UserRole).toString() == device_name) {
            item->setBackground(QColor(255, 192, 192));  // Light red
            status_label_->setText(QString("Status: Disconnected from %1").arg(device_name));
            break;
        }
    }
}

void MainWindow::onSyncStarted() {
    sync_status_label_->setText("Syncing files...");
    sync_status_label_->setStyleSheet("color: blue; font-weight: bold;");
}

void MainWindow::onSyncProgress(int progress) {
    sync_status_label_->setText(QString("Syncing... (%1%)").arg(progress));
    sync_status_label_->setStyleSheet("color: blue; font-weight: bold;");
}

void MainWindow::onSyncCompleted() {
    sync_status_label_->setText("Sync idle (waiting for changes)");
    sync_status_label_->setStyleSheet("color: green; font-weight: bold;");
}

void MainWindow::onSyncError(const QString& error_message) {
    sync_status_label_->setText(QString("Error: %1").arg(error_message));
    sync_status_label_->setStyleSheet("color: red; font-weight: bold;");
}

void MainWindow::onPeriodicCheck() {
    // This is called every 10 seconds to check SSL certificates and device status
    checkSSLCertificates();
    
    // Tell the sync worker to refresh SSL certs and device status
    if (sync_worker_) {
        QMetaObject::invokeMethod(sync_worker_.get(), "checkSSLCertificatesAndDevices", 
                                  Qt::QueuedConnection);
    }
    
    // Update status to "online" if we're syncing
    if (device_list_widget_->count() > 0) {
        status_label_->setText("Status: Online");
    }
}

void MainWindow::checkSSLCertificates() {
    // Check the .syncflow/certs directory for any new certificates
    QDir certs_dir(QDir::home().filePath(".syncflow/certs"));
    
    if (certs_dir.exists()) {
        QStringList cert_files = certs_dir.entryList(QStringList() << "*.pem" << "*.crt", QDir::Files);
        
        // Emit a signal or refresh status based on cert discovery
        if (!cert_files.isEmpty() && sync_worker_) {
            // Optionally trigger device list refresh
            // (In production, would check for new certificates not yet in trusted list)
            status_label_->setText(QString("Status: Online (%1 certs found)").arg(cert_files.count()));
        }
    }
}

void MainWindow::onSettingsClicked() {
    SettingsDialog dialog(this);
    dialog.setDeviceName(device_name_);
    if (dialog.exec() == QDialog::Accepted) {
        device_name_ = dialog.getDeviceName();
        saveSettings();
    }
}

void MainWindow::onApproveClicked() {
    QListWidgetItem* item = device_list_widget_->currentItem();
    if (!item) {
        QMessageBox::information(this, "No Selection", "Please select a device to approve.");
        return;
    }

    QString device_name = item->data(Qt::UserRole).toString();
    DeviceApprovalDialog dialog(device_name, "0.0.0.0", "fingerprint_hash_here", this);
    dialog.exec();
}

void MainWindow::onRemoveClicked() {
    QListWidgetItem* item = device_list_widget_->currentItem();
    if (!item) {
        QMessageBox::information(this, "No Selection", "Please select a device to remove.");
        return;
    }

    int result = QMessageBox::question(this, "Confirm Removal",
        QString("Remove '%1' from trusted devices?").arg(item->text()));

    if (result == QMessageBox::Yes) {
        delete item;
    }
}

void MainWindow::startSyncWorker() {
    if (!worker_thread_) return;

    // Ensure device name is set on worker then start it in the worker thread
    QMetaObject::invokeMethod(sync_worker_.get(), "setDeviceName", Qt::QueuedConnection,
                              Q_ARG(QString, device_name_));
    QMetaObject::invokeMethod(sync_worker_.get(), "setSyncPaths", Qt::QueuedConnection,
                              Q_ARG(QString, QString()), Q_ARG(QString, QString()));
    QMetaObject::invokeMethod(sync_worker_.get(), "setSecurityEnabled", Qt::QueuedConnection,
                              Q_ARG(bool, true));
    QMetaObject::invokeMethod(sync_worker_.get(), "start", Qt::QueuedConnection);

    if (!worker_thread_->isRunning()) {
        worker_thread_->start();
    }
    
    status_label_->setText("Status: Online");
}

void MainWindow::closeEvent(QCloseEvent* event) {
    saveSettings();
    if (periodic_timer_) {
        periodic_timer_->stop();
    }
    if (worker_thread_) {
        QMetaObject::invokeMethod(sync_worker_.get(), "stop", Qt::QueuedConnection);
        worker_thread_->quit();
        worker_thread_->wait(5000);
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::loadSettings() {
    QSettings settings("Syncflow", "Syncflow");
    device_name_ = settings.value("device_name", device_name_).toString();
}

void MainWindow::saveSettings() {
    QSettings settings("Syncflow", "Syncflow");
    settings.setValue("device_name", device_name_);
    settings.sync();
}

}  // namespace syncflow::gui
