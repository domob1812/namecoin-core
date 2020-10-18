// TODO: figure out which of these includes are actually still necessary for name_list
#include <qt/managenamespage.h>
#include <qt/forms/ui_managenamespage.h>

#include <base58.h>
#include <consensus/validation.h> // cs_main
#include <names/common.h>
#include <qt/csvmodelwriter.h>
#include <qt/guiutil.h>
#include <qt/nametablemodel.h>
#include <qt/platformstyle.h>
#include <qt/walletmodel.h>
#include <node/ui_interface.h>
#include <univalue.h>
#include <wallet/wallet.h>

#include <QMessageBox>
#include <QMenu>
#include <QSortFilterProxyModel>

// TODO: figure out which of these members are actually still necessary for name_list
ManageNamesPage::ManageNamesPage(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    platformStyle(platformStyle),
    ui(new Ui::ManageNamesPage),
    model(nullptr),
    walletModel(nullptr),
    proxyModel(nullptr)
{
    ui->setupUi(this);

    // Context menu actions
    QAction *copyNameAction = new QAction(tr("Copy &Name"), this);
    QAction *copyValueAction = new QAction(tr("Copy &Value"), this);
    QAction *renewNameAction = new QAction(tr("&Renew Name"), this);

    // Build context menu
    contextMenu = new QMenu();
    contextMenu->addAction(copyNameAction);
    contextMenu->addAction(copyValueAction);
    contextMenu->addAction(renewNameAction);

    // Connect signals for context menu actions
    connect(copyNameAction, &QAction::triggered, this, &ManageNamesPage::onCopyNameAction);
    connect(copyValueAction, &QAction::triggered, this, &ManageNamesPage::onCopyValueAction);
    connect(renewNameAction, &QAction::triggered, this, &ManageNamesPage::onRenewNameAction);

    connect(ui->tableView, &QTableView::customContextMenuRequested, this, &ManageNamesPage::contextualMenu);
    ui->tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);

    ui->tableView->installEventFilter(this);
}

ManageNamesPage::~ManageNamesPage()
{
    delete ui;
}

void ManageNamesPage::setModel(WalletModel *walletModel)
{
    this->walletModel = walletModel;
    model = walletModel->getNameTableModel();

    proxyModel = new QSortFilterProxyModel(this);
    proxyModel->setSourceModel(model);
    proxyModel->setDynamicSortFilter(true);
    proxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
    proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);

    ui->tableView->setModel(proxyModel);
    ui->tableView->sortByColumn(0, Qt::AscendingOrder);

    ui->tableView->horizontalHeader()->setHighlightSections(false);

    // Set column widths
    ui->tableView->horizontalHeader()->resizeSection(
            NameTableModel::Name, 320);
    ui->tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    connect(ui->tableView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &ManageNamesPage::selectionChanged);

    selectionChanged();
}

bool ManageNamesPage::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::FocusIn)
    {
        if (object == ui->tableView)
        {
        }
    }
    return QWidget::eventFilter(object, event);
}

void ManageNamesPage::selectionChanged()
{
    // Set button states based on selected tab and selection
    QTableView *table = ui->tableView;
    if (!table->selectionModel())
        return;

    const bool state = table->selectionModel()->hasSelection();
    //ui->configureNameButton->setEnabled(state);
    //ui->renewNameButton->setEnabled(state);
}

void ManageNamesPage::contextualMenu(const QPoint &point)
{
    QModelIndex index = ui->tableView->indexAt(point);
    if (index.isValid())
        contextMenu->exec(QCursor::pos());
}

void ManageNamesPage::onCopyNameAction()
{
    GUIUtil::copyEntryData(ui->tableView, NameTableModel::Name);
}

void ManageNamesPage::onCopyValueAction()
{
    GUIUtil::copyEntryData(ui->tableView, NameTableModel::Value);
}

void ManageNamesPage::onRenewNameAction()
{
    QModelIndexList indexes = GUIUtil::getEntryData(ui->tableView, NameTableModel::Name);

    if (indexes.isEmpty())
        return;

    QString msg;
    QString title;

    if (indexes.size() == 1)
    {
        const QString &name = indexes.at(0).data(Qt::EditRole).toString();

        msg = tr ("Are you sure you want to renew the name <b>%1</b>?")
            .arg (GUIUtil::HtmlEscape (name));
        title = tr ("Confirm name renewal");
    }
    else
    {
        msg = tr ("Are you sure you want to renew multiple names simultaneously?  This will reveal common ownership of the renewed names (bad for anonymity).");
        title = tr ("Confirm multiple name renewal");
    }

    QMessageBox::StandardButton res;
    res = QMessageBox::question (this, title, msg,
                                 QMessageBox::Yes | QMessageBox::Cancel,
                                 QMessageBox::Cancel);
    if (res != QMessageBox::Yes)
        return;

    WalletModel::UnlockContext ctx(walletModel->requestUnlock ());
    if (!ctx.isValid ())
        return;

    for (QModelIndexList::iterator i = indexes.begin(); i != items.end(); i++)
    {
        const QString &name = i.data(Qt::EditRole).toString();

        const QString err_msg = model->renew(name);
        if (!err_msg.isEmpty() && err_msg != "ABORTED")
        {
            QMessageBox::critical(this, tr("Name renew error"), err_msg);
            return;
        }
    }
}

void ManageNamesPage::exportClicked()
{
    // CSV is currently the only supported format
    QString suffixOut = "";
    QString filename = GUIUtil::getSaveFileName(
            this,
            tr("Export Registered Names Data"),
            QString(),
            tr("Comma separated file (*.csv)"),
            &suffixOut);

    if (filename.isNull())
        return;

    CSVModelWriter writer(filename);

    // name, column, role
    writer.setModel(proxyModel);
    writer.addColumn("Name", NameTableModel::Name, Qt::EditRole);
    writer.addColumn("Value", NameTableModel::Value, Qt::EditRole);
    writer.addColumn("Expires In", NameTableModel::ExpiresIn, Qt::EditRole);
    writer.addColumn("Name Status", NameTableModel::NameStatus, Qt::EditRole);

    if (!writer.write())
    {
        QMessageBox::critical(this, tr("Error exporting"), tr("Could not write to file %1.").arg(filename),
                              QMessageBox::Abort, QMessageBox::Abort);
    }
}
