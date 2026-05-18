#include "role_select_dialog.h"
#include "ui_role_select_dialog.h"

RoleSelectDialog::RoleSelectDialog(int currentRole, QWidget *parent)
    : QDialog(parent), ui(new Ui::RoleSelectDialog), m_selectedRole(currentRole)
{
    ui->setupUi(this);
}

RoleSelectDialog::~RoleSelectDialog() { delete ui; }

void RoleSelectDialog::on_pb_broadcaster_clicked()
{
    m_selectedRole = 1;
    accept();
}

void RoleSelectDialog::on_pb_viewer_clicked()
{
    m_selectedRole = 0;
    accept();
}
