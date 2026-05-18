#ifndef ROLE_SELECT_DIALOG_H
#define ROLE_SELECT_DIALOG_H

#include <QDialog>

namespace Ui { class RoleSelectDialog; }

class RoleSelectDialog : public QDialog
{
    Q_OBJECT
public:
    explicit RoleSelectDialog(int currentRole, QWidget *parent = nullptr);
    ~RoleSelectDialog();
    int selectedRole() const { return m_selectedRole; }

private slots:
    void on_pb_broadcaster_clicked();
    void on_pb_viewer_clicked();

private:
    Ui::RoleSelectDialog *ui;
    int m_selectedRole;
};

#endif
