#include "nametablemodel.h"

#include "guiutil.h"
#include "walletmodel.h"
#include "guiconstants.h"
#include "wallet/wallet.h"
#include "ui_interface.h"
#include "platformstyle.h"

#include "main.h"
#include "names/common.h"
#include "util.h"
#include "protocol.h"
#include "rpc/server.h"

#include <univalue.h>

#include <QTimer>
#include <QObject>

// ExpiresIn column is right-aligned as it contains numbers
static int column_alignments[] = {
        Qt::AlignLeft|Qt::AlignVCenter,     // Name
        Qt::AlignLeft|Qt::AlignVCenter,     // Value
        Qt::AlignRight|Qt::AlignVCenter     // Expires in
    };

struct NameTableEntryLessThan
{
    bool operator()(const NameTableEntry &a, const NameTableEntry &b) const
    {
        return a.name < b.name;
    }
    bool operator()(const NameTableEntry &a, const QString &b) const
    {
        return a.name < b;
    }
    bool operator()(const QString &a, const NameTableEntry &b) const
    {
        return a < b.name;
    }
};

// Returns true if new height is better
/*static*/ bool NameTableEntry::CompareHeight(int nOldHeight, int nNewHeight)
{
    if (nOldHeight == NAME_NON_EXISTING)
        return true;

    // We use optimistic way, assuming that unconfirmed transaction will eventually become confirmed,
    // so we update the name in the table immediately. Ideally we need a separate way of displaying
    // unconfirmed names (e.g. grayed out)
    if (nNewHeight == NAME_UNCONFIRMED)
        return true;

    // Here we rely on the fact that dummy height values are always negative
    return nNewHeight > nOldHeight;
}

// Private implementation
class NameTablePriv
{
public:
    CWallet *wallet;
    QList<NameTableEntry> cachedNameTable;
    NameTableModel *parent;

    NameTablePriv(CWallet *wallet, NameTableModel *parent):
        wallet(wallet), parent(parent) {}

    void refreshNameTable()
    {
        cachedNameTable.clear();
        std::map< std::string, NameTableEntry > vNamesO;

        UniValue names;
        try {
            names = name_list(NullUniValue, false).get_array();
        } catch (const UniValue& e) {
            LogPrintf ("name_list lookup error: %s\n", e.getValStr().c_str());
        }

        // pull all our names from wallet
        for (unsigned int idx = 0; idx < names.size(); idx++) {
            const UniValue& v = names[idx];
            std::string name = find_value ( v, "name").get_str();
            std::string data = find_value ( v, "value").get_str();
            int height = find_value ( v, "height").get_int();
            vNamesO[name] = NameTableEntry(name, data, height);
        }

        // Add existing names
        BOOST_FOREACH(const PAIRTYPE(std::string, NameTableEntry)& item, vNamesO)
            cachedNameTable.append(item.second);

        // Add pending names (name_new)
        BOOST_FOREACH(const PAIRTYPE(std::string, NameNewReturn)& item, pendingNameFirstUpdate)
            cachedNameTable.append(
                NameTableEntry(item.first,
                               item.second.data,
                               NameTableEntry::NAME_NEW));

        // qLowerBound() and qUpperBound() require our cachedNameTable list to be sorted in asc order
        qSort(cachedNameTable.begin(), cachedNameTable.end(), NameTableEntryLessThan());
    }

    void refreshName(const std::vector<unsigned char> &inName)
    {

        LOCK(cs_main);

        NameTableEntry nameObj(ValtypeToString(inName),
                               std::string(""),
                               NameTableEntry::NAME_NON_EXISTING);

        CNameData data;
        {
            LOCK (cs_main);
            if (!pcoinsTip->GetName (inName, data))
            {
                LogPrintf ("name not found: '%s'\n", ValtypeToString (inName).c_str());
                return;
            }

            nameObj = NameTableEntry(ValtypeToString(inName),
                                     ValtypeToString(data.getValue ()),
                                     data.getHeight ());
        }

        // Find name in model
        QList<NameTableEntry>::iterator lower = qLowerBound(
            cachedNameTable.begin(), cachedNameTable.end(), nameObj.name, NameTableEntryLessThan());
        QList<NameTableEntry>::iterator upper = qUpperBound(
            cachedNameTable.begin(), cachedNameTable.end(), nameObj.name, NameTableEntryLessThan());
        bool inModel = (lower != upper);

        if (inModel)
        {
            // In model - update or delete
            if (nameObj.nHeight != NameTableEntry::NAME_NON_EXISTING)
            {
                LogPrintf ("refreshName result : %s - refreshed in the table\n", qPrintable(nameObj.name));
                updateEntry(nameObj.name, nameObj.value, nameObj.nHeight, CT_UPDATED);
            }
            else
            {
                LogPrintf("refreshName result : %s - deleted from the table\n", qPrintable(nameObj.name));
                updateEntry(nameObj.name, nameObj.value, nameObj.nHeight, CT_DELETED);
            }
        }
        else
        {
            // Not in model - add or do nothing
            if (nameObj.nHeight != NameTableEntry::NAME_NON_EXISTING)
            {
                LogPrintf("refreshName result : %s - added to the table\n", qPrintable(nameObj.name));
                updateEntry(nameObj.name, nameObj.value, nameObj.nHeight, CT_NEW);
            }
            else
            {
                LogPrintf("refreshName result : %s - ignored (not in the table)\n", qPrintable(nameObj.name));
            }
        }
    }

    void
    updateEntry(const QString &name, const QString &value,
                int nHeight, int status, int *outNewRowIndex = NULL)
    {
        // Find name in model
        QList<NameTableEntry>::iterator lower = qLowerBound(
            cachedNameTable.begin(), cachedNameTable.end(), name, NameTableEntryLessThan());
        QList<NameTableEntry>::iterator upper = qUpperBound(
            cachedNameTable.begin(), cachedNameTable.end(), name, NameTableEntryLessThan());
        int lowerIndex = (lower - cachedNameTable.begin());
        int upperIndex = (upper - cachedNameTable.begin());
        bool inModel = (lower != upper);

        switch(status)
        {
        case CT_NEW:
            if (inModel)
            {
                if (outNewRowIndex)
                {
                    *outNewRowIndex = parent->index(lowerIndex, 0).row();
                    // HACK: ManageNamesPage uses this to ensure updating and get selected row,
                    // so we do not write warning into the log in this case
                }
                else {
                    LogPrintf ("Warning: NameTablePriv::updateEntry: Got CT_NOW, but entry is already in model\n");
                }
                break;
            }
            parent->beginInsertRows(QModelIndex(), lowerIndex, lowerIndex);
            cachedNameTable.insert(lowerIndex, NameTableEntry(name, value, nHeight));
            parent->endInsertRows();
            if (outNewRowIndex)
                *outNewRowIndex = parent->index(lowerIndex, 0).row();
            break;
        case CT_UPDATED:
            if (!inModel)
            {
                LogPrintf ("Warning: NameTablePriv::updateEntry: Got CT_UPDATED, but entry is not in model\n");
                break;
            }
            lower->name = name;
            lower->value = value;
            lower->nHeight = nHeight;
            parent->emitDataChanged(lowerIndex);
            break;
        case CT_DELETED:
            if (!inModel)
            {
                LogPrintf ("Warning: NameTablePriv::updateEntry: Got CT_DELETED, but entry is not in model\n");
                break;
            }
            parent->beginRemoveRows(QModelIndex(), lowerIndex, upperIndex-1);
            cachedNameTable.erase(lower, upper);
            parent->endRemoveRows();
            break;
        }
    }

    int size()
    {
        return cachedNameTable.size();
    }

    NameTableEntry *index(int idx)
    {
        if (idx >= 0 && idx < cachedNameTable.size())
        {
            return &cachedNameTable[idx];
        }
        else
        {
            return NULL;
        }
    }
};

NameTableModel::NameTableModel(const PlatformStyle *platformStyle, CWallet* wallet, WalletModel *parent):
        QAbstractTableModel(parent),
        wallet(wallet),
        walletModel(parent),
        priv(new NameTablePriv(wallet, this)),
        platformStyle(platformStyle)
{
    columns << tr("Name") << tr("Value") << tr("Expires in");
    priv = new NameTablePriv(wallet, this);
    priv->refreshNameTable();

    QTimer *timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(updateExpiration()));
    timer->start(MODEL_UPDATE_DELAY);

    subscribeToCoreSignals();
}

NameTableModel::~NameTableModel()
{
    unsubscribeFromCoreSignals();
    delete priv;
}

void NameTableModel::updateExpiration()
{
    int nBestHeight = chainActive.Height();
    if (nBestHeight != cachedNumBlocks)
    {
        LOCK(cs_main);

        cachedNumBlocks = nBestHeight;
        // Blocks came in since last poll.
        // Delete expired names
        for (int i = 0, n = priv->size(); i < n; i++)
        {
            NameTableEntry *item = priv->index(i);
            if (!item->HeightValid())
                continue;       // Currently, unconfirmed names do not expire in the table
            int nHeight = item->nHeight;

            // NOTE: the line below used to be: GetExpirationDepth(nHeight)
            // I changed it to just nHeight for now
            // int GetExpirationDepth(int nHeight) {
            //     if (nHeight < 24000)
            //         return 12000;
            //     if (nHeight < 48000)
            //         return nHeight - 12000;
            //     return 36000;
            // }

            const Consensus::Params& params = Params ().GetConsensus ();
            if (nHeight + params.rules->NameExpirationDepth (nHeight) - nBestHeight <= 0)
            {
            //if (nHeight + 36000 - nBestHeight <= 0)
            //{
                priv->updateEntry(item->name, item->value, item->nHeight, CT_DELETED);
                // Data array changed - restart scan
                n = priv->size();
                i = -1;
            }

        }
        // Invalidate expiration counter for all rows.
        // Qt is smart enough to only actually request the data for the
        // visible rows.
        //emit
        dataChanged(index(0, ExpiresIn), index(priv->size()-1, ExpiresIn));
    }
}

void NameTableModel::updateTransaction(const QString &hash, int status)
{
    uint256 hash256;
    std::string strHash = hash.toStdString();
    hash256.SetHex(strHash);

    CTransaction tx;
    {
        LOCK(wallet->cs_wallet);
        // Find transaction in wallet
        std::map<uint256, CWalletTx>::iterator mi = wallet->mapWallet.find(hash256);
        if (mi == wallet->mapWallet.end())
        {
            LogPrintf ("tx %s has no name in wallet\n", strHash);
            return;
        }
        tx = mi->second;
    }

    valtype valName;
    const std::vector<CTxOut> vout = tx.vout;
    for (std::vector<CTxOut>::const_iterator it(vout.begin()); it != vout.end(); ++it)
    {
        if (!CNameScript::isNameScript(it->scriptPubKey))
        {
            continue;
        }

        CNameScript nameScript(it->scriptPubKey);
        switch (nameScript.getNameOp ())
        {
            case OP_NAME_NEW:
                break;

            case OP_NAME_FIRSTUPDATE:
            case OP_NAME_UPDATE:
                priv->refreshName(nameScript.getOpName ());
                break;

            default:
                assert (false);
        }
    }

}

int NameTableModel::rowCount(const QModelIndex &parent /*= QModelIndex()*/) const
{
    Q_UNUSED(parent);
    return priv->size();
}

int NameTableModel::columnCount(const QModelIndex &parent /*= QModelIndex()*/) const
{
    Q_UNUSED(parent);
    return columns.length();
}

QVariant
NameTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    NameTableEntry *rec = static_cast<NameTableEntry*>(index.internalPointer());

    if (role == Qt::DisplayRole || role == Qt::EditRole)
    {
        switch(index.column())
        {
        case Name:
            return rec->name;
        case Value:
            return rec->value;
        case ExpiresIn:
            if (!rec->HeightValid()) {
                return QVariant();
            }
            else {
                int nBestHeight = chainActive.Height();
                // OG: return rec->nHeight + GetDisplayExpirationDepth(rec->nHeight)
                // - pindexBest->nHeight;
                return rec->nHeight + 36000 - nBestHeight;
            }
        }
    }
    return QVariant();
}

QVariant
NameTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal)
    {
        if (role == Qt::DisplayRole)
        {
            return columns[section];
        }
        else if (role == Qt::TextAlignmentRole)
        {
            return column_alignments[section];
        }
        else if (role == Qt::ToolTipRole)
        {
            switch(section)
            {
            case Name:
                return tr("Name registered using Namecoin.");
            case Value:
                return tr("Data associated with the name.");
            case ExpiresIn:
                return tr("Number of blocks, after which the name will expire."
                          " Update name to renew it.\nEmpty cell means pending"
                          " (awaiting automatic name_firstupdate or awaiting "
                          "network confirmation).");
            }
        }
    }
    return QVariant();
}

Qt::ItemFlags
NameTableModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return 0;
    //NameTableEntry *rec = static_cast<NameTableEntry*>(index.internalPointer());

    return Qt::ItemIsSelectable | Qt::ItemIsEnabled;
}

QModelIndex
NameTableModel::index(int row, int column, const QModelIndex &parent /* = QModelIndex()*/) const
{
    Q_UNUSED(parent);
    NameTableEntry *data = priv->index(row);
    if (data)
    {
        return createIndex(row, column, priv->index(row));
    }
    else
    {
        return QModelIndex();
    }
}

// queue notifications to show a non freezing progress dialog e.g. for rescan
struct TransactionNotification
{
public:
    TransactionNotification() {}
    TransactionNotification(uint256 hash, ChangeType status, bool showTransaction):
        hash(hash), status(status), showTransaction(showTransaction) {}

    void invoke(NameTableModel *ntm)
    {
        QString strHash = QString::fromStdString(hash.GetHex());
        QMetaObject::invokeMethod(ntm, "updateTransaction", Qt::QueuedConnection,
                                  Q_ARG(QString, strHash),
                                  Q_ARG(int, status));
    }
private:
    uint256 hash;
    ChangeType status;
    bool showTransaction;
};

static bool fQueueNotifications = false;
static std::vector< TransactionNotification > vQueueNotifications;

static void NotifyTransactionChanged(NameTableModel *ntm, CWallet *wallet, const uint256 &hash, ChangeType status)
{
    // Find transaction in wallet
    std::map<uint256, CWalletTx>::iterator mi = wallet->mapWallet.find(hash);
    // Determine whether to show transaction or not (determine this here so that no relocking is needed in GUI thread)
    bool inWallet = mi != wallet->mapWallet.end();
    // bool showTransaction = (inWallet && TransactionRecord::showTransaction(mi->second));

    // TransactionNotification notification(hash, status, showTransaction);
    TransactionNotification notification(hash, status, inWallet);

    if (fQueueNotifications)
    {
        vQueueNotifications.push_back(notification);
        return;
    }
    notification.invoke(ntm);
}


void
NameTableModel::updateEntry(const QString &name, const QString &value,
                            int nHeight, int status, int *outNewRowIndex /*= NULL*/)
{
    priv->updateEntry(name, value, nHeight, status, outNewRowIndex);
}

void
NameTableModel::emitDataChanged(int idx)
{
    //emit
    dataChanged(index(idx, 0), index(idx, columns.length()-1));
}

void
NameTableModel::subscribeToCoreSignals()
{
    // Connect signals to wallet
    wallet->NotifyTransactionChanged.connect(boost::bind(NotifyTransactionChanged, this, _1, _2, _3));
    // wallet->ShowProgress.connect(boost::bind(ShowProgress, this, _1, _2));
}

void
NameTableModel::unsubscribeFromCoreSignals()
{
    // Disconnect signals from wallet
    wallet->NotifyTransactionChanged.disconnect(boost::bind(NotifyTransactionChanged, this, _1, _2, _3));
    // wallet->ShowProgress.disconnect(boost::bind(ShowProgress, this, _1, _2));
}
