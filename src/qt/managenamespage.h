#ifndef MANAGENAMESPAGE_H
#define MANAGENAMESPAGE_H

#include "platformstyle.h"

#include <QDialog>

class WalletModel;
class NameTableModel;

namespace Ui {
    class ManageNamesPage;
}

QT_BEGIN_NAMESPACE
class QTableView;
class QItemSelection;
class QSortFilterProxyModel;
class QMenu;
class QModelIndex;
QT_END_NAMESPACE

/** Page for managing names */
class ManageNamesPage : public QDialog
{
    Q_OBJECT

public:
    explicit ManageNamesPage(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~ManageNamesPage();

    void setModel(WalletModel *walletModel);

private:
    const PlatformStyle *platformStyle;
    Ui::ManageNamesPage *ui;
    NameTableModel *model;
    WalletModel *walletModel;
    QSortFilterProxyModel *proxyModel;
    QMenu *contextMenu;

public Q_SLOTS:
    void exportClicked();

private Q_SLOTS:
    void on_submitNameButton_clicked();

    bool eventFilter(QObject *object, QEvent *event);
    void selectionChanged();

    /** Spawn contextual menu (right mouse menu) for name table entry */
    void contextualMenu(const QPoint &point);

    void onCopyNameAction();
    void onCopyValueAction();
    void on_configureNameButton_clicked();
    void on_renewNameButton_clicked();
};

#endif // MANAGENAMESPAGE_H
