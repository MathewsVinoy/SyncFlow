#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QComboBox>

namespace syncflow {
namespace gui {

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    QString getDeviceName() const;
    void setDeviceName(const QString& name);

    QString getSourcePath() const;
    void setSourcePath(const QString& path);

    QString getReceiveDir() const;
    void setReceiveDir(const QString& path);

    bool isFileSyncEnabled() const;
    void setFileSyncEnabled(bool enabled);

    bool isSecurityEnabled() const;
    void setSecurityEnabled(bool enabled);

    bool requiresDeviceApproval() const;
    void setRequireDeviceApproval(bool require);

private slots:
    void onBrowseSourceClicked();
    void onBrowseReceiveDirClicked();
    void onApplyClicked();
    void onCancelClicked();
    void onResetClicked();

private:
    void setupUI();
    void loadSettings();

    QLineEdit* device_name_edit_;
    QLineEdit* source_path_edit_;
    QLineEdit* receive_dir_edit_;
    QCheckBox* file_sync_checkbox_;
    QCheckBox* security_checkbox_;
    QCheckBox* require_approval_checkbox_;
    QPushButton* browse_source_button_;
    QPushButton* browse_receive_button_;
    QPushButton* apply_button_;
    QPushButton* cancel_button_;
    QPushButton* reset_button_;
};

} // namespace gui
} // namespace syncflow
