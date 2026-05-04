#pragma once

#include <QObject>
#include <QString>
#include <memory>
#include <thread>

namespace syncflow {
namespace networking {
class PeerNode;
}

namespace gui {

class SyncWorker : public QObject {
    Q_OBJECT

public:
    explicit SyncWorker(QObject* parent = nullptr);
    ~SyncWorker() override;

public slots:
    void start();
    void stop();
    void approvePeer(const QString& device_id);
    void removePeer(const QString& device_id);
    void setDeviceName(const QString& name);
    void setSyncPaths(const QString& source_path, const QString& receive_dir);
    void setSecurityEnabled(bool enabled);
    void checkSSLCertificatesAndDevices();  // Called periodically to refresh SSL certs and device status

signals:
    void deviceDiscovered(const QString& device_name, const QString& device_ip, const QString& fingerprint);
    void deviceConnected(const QString& device_name);
    void deviceDisconnected(const QString& device_name);
    void syncStarted();
    void syncProgress(int progress);
    void syncCompleted();
    void syncError(const QString& error_message);
    void statusChanged(const QString& status);

private:
    void performDiscovery();

    bool is_running_;
    std::unique_ptr<syncflow::networking::PeerNode> peer_node_;
    QString device_name_;
    QString source_path_;
    QString receive_dir_;
    bool security_enabled_;
    std::thread discovery_thread_;
};

} // namespace gui
} // namespace syncflow
