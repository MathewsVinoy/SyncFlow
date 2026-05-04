#pragma once

#include <QObject>
#include <QString>
#include <memory>

namespace syncflow::networking {
class PeerNode;
}

namespace syncflow::gui {

class SyncWorker : public QObject {
    Q_OBJECT

public:
    explicit SyncWorker();
    ~SyncWorker() override;

public slots:
    void start(const QString& device_name);
    void stop();
    void approvePeer(const QString& fingerprint);
    void removePeer(const QString& fingerprint);

signals:
    void deviceDiscovered(const QString& device_name, const QString& device_ip);
    void deviceConnected(const QString& device_name);
    void deviceDisconnected(const QString& device_name);
    void syncStarted(const QString& device_name);
    void syncProgress(const QString& file_name, int progress);
    void syncCompleted(const QString& device_name);
    void syncError(const QString& error_message);
    void statusChanged(const QString& status);

private:
    void monitorPeer();

    std::unique_ptr<syncflow::networking::PeerNode> peer_node_;
    bool is_running_;
};

}  // namespace syncflow::gui
