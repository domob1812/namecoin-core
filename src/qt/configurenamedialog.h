#ifndef CONFIGURENAMEDIALOG_H
#define CONFIGURENAMEDIALOG_H

#include "platformstyle.h"

#include <QDialog>

namespace Ui {
    class ConfigureNameDialog;
}

class WalletModel;

/** Dialog for editing an address and associated information.
 */
class ConfigureNameDialog : public QDialog
{
    Q_OBJECT

public:

    explicit ConfigureNameDialog(const PlatformStyle *platformStyle,
                                 const QString &_name, const QString &data,
                                 bool _firstUpdate, QWidget *parent = 0);
    ~ConfigureNameDialog();

    void setModel(WalletModel *walletModel);
    const QString &getReturnData() const { return returnData; }

public Q_SLOTS:
    void accept();
    void reject();
    void on_addressBookButton_clicked();
    void on_pasteButton_clicked();

private:
    Ui::ConfigureNameDialog *ui;
    const PlatformStyle *platformStyle;
    QString returnData;
    WalletModel *walletModel;
    QString name;
    bool firstUpdate;
};

#endif // CONFIGURENAMEDIALOG_H
