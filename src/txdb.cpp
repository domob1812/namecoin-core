// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <txdb.h>

#include <names/encoding.h>
#include <node/ui_interface.h>
#include <pow.h>
#include <random.h>
#include <script/names.h>
#include <shutdown.h>
#include <uint256.h>
#include <util/memory.h>
#include <util/system.h>
#include <util/translation.h>
#include <util/vector.h>
#include <validation.h>

#include <stdint.h>

#include <boost/thread/thread.hpp>

static const char DB_COIN = 'C';
static const char DB_COINS = 'c';
static const char DB_BLOCK_FILES = 'f';
static const char DB_BLOCK_INDEX = 'b';

static const char DB_NAME = 'n';
static const char DB_NAME_HISTORY = 'h';
static const char DB_NAME_EXPIRY = 'x';

static const char DB_BEST_BLOCK = 'B';
static const char DB_HEAD_BLOCKS = 'H';
static const char DB_FLAG = 'F';
static const char DB_REINDEX_FLAG = 'R';
static const char DB_LAST_BLOCK = 'l';

namespace {

struct CoinEntry {
    COutPoint* outpoint;
    char key;
    explicit CoinEntry(const COutPoint* ptr) : outpoint(const_cast<COutPoint*>(ptr)), key(DB_COIN)  {}

    SERIALIZE_METHODS(CoinEntry, obj) { READWRITE(obj.key, obj.outpoint->hash, VARINT(obj.outpoint->n)); }
};

}

CCoinsViewDB::CCoinsViewDB(fs::path ldb_path, size_t nCacheSize, bool fMemory, bool fWipe) :
    m_db(MakeUnique<CDBWrapper>(ldb_path, nCacheSize, fMemory, fWipe, true)),
    m_ldb_path(ldb_path),
    m_is_memory(fMemory) { }

void CCoinsViewDB::ResizeCache(size_t new_cache_size)
{
    // Have to do a reset first to get the original `m_db` state to release its
    // filesystem lock.
    m_db.reset();
    m_db = MakeUnique<CDBWrapper>(
        m_ldb_path, new_cache_size, m_is_memory, /*fWipe*/ false, /*obfuscate*/ true);
}

bool CCoinsViewDB::GetCoin(const COutPoint &outpoint, Coin &coin) const {
    return m_db->Read(CoinEntry(&outpoint), coin);
}

bool CCoinsViewDB::HaveCoin(const COutPoint &outpoint) const {
    return m_db->Exists(CoinEntry(&outpoint));
}

uint256 CCoinsViewDB::GetBestBlock() const {
    uint256 hashBestChain;
    if (!m_db->Read(DB_BEST_BLOCK, hashBestChain))
        return uint256();
    return hashBestChain;
}

std::vector<uint256> CCoinsViewDB::GetHeadBlocks() const {
    std::vector<uint256> vhashHeadBlocks;
    if (!m_db->Read(DB_HEAD_BLOCKS, vhashHeadBlocks)) {
        return std::vector<uint256>();
    }
    return vhashHeadBlocks;
}

bool CCoinsViewDB::GetName(const valtype &name, CNameData& data) const {
    return m_db->Read(std::make_pair(DB_NAME, name), data);
}

bool CCoinsViewDB::GetNameHistory(const valtype &name, CNameHistory& data) const {
    assert (fNameHistory);
    return m_db->Read(std::make_pair(DB_NAME_HISTORY, name), data);
}

bool CCoinsViewDB::GetNamesForHeight(unsigned nHeight, std::set<valtype>& names) const {
    names.clear();

    /* It seems that there are no "const iterators" for LevelDB.  Since we
       only need read operations on it, use a const-cast to get around
       that restriction.  */
    std::unique_ptr<CDBIterator> pcursor(const_cast<CDBWrapper*>(m_db.get())->NewIterator());

    const CNameCache::ExpireEntry seekEntry(nHeight, valtype ());
    pcursor->Seek(std::make_pair(DB_NAME_EXPIRY, seekEntry));

    for (; pcursor->Valid(); pcursor->Next())
    {
        std::pair<char, CNameCache::ExpireEntry> key;
        if (!pcursor->GetKey(key) || key.first != DB_NAME_EXPIRY)
            break;
        const CNameCache::ExpireEntry& entry = key.second;

        assert (entry.nHeight >= nHeight);
        if (entry.nHeight > nHeight)
          break;

        const valtype& name = entry.name;
        if (names.count(name) > 0)
            return error("%s : duplicate name %s in expire index",
                         __func__, EncodeNameForMessage(name));
        names.insert(name);
    }

    return true;
}

class CDbNameIterator : public CNameIterator
{

private:

    /** The backing LevelDB iterator.  */
    std::unique_ptr<CDBIterator> iter;

public:

    /**
     * Construct a new name iterator for the database.
     * @param db The database to create the iterator for.
     */
    CDbNameIterator(const CDBWrapper& db);

    /* Implement iterator methods.  */
    void seek (const valtype& start);
    bool next (valtype& name, CNameData& data);

};

CDbNameIterator::CDbNameIterator(const CDBWrapper& db)
    : iter(const_cast<CDBWrapper*>(&db)->NewIterator())
{
    seek(valtype());
}

void CDbNameIterator::seek(const valtype& start) {
    iter->Seek(std::make_pair(DB_NAME, start));
}

bool CDbNameIterator::next(valtype& name, CNameData& data) {
    if (!iter->Valid())
        return false;

    std::pair<char, valtype> key;
    if (!iter->GetKey(key) || key.first != DB_NAME)
        return false;
    name = key.second;

    if (!iter->GetValue(data))
        return error("%s : failed to read data from iterator", __func__);

    iter->Next ();
    return true;
}

CNameIterator* CCoinsViewDB::IterateNames() const {
    return new CDbNameIterator(*m_db);
}

bool CCoinsViewDB::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock, const CNameCache &names) {
    CDBBatch batch(*m_db);
    size_t count = 0;
    size_t changed = 0;
    size_t batch_size = (size_t)gArgs.GetArg("-dbbatchsize", nDefaultDbBatchSize);
    int crash_simulate = gArgs.GetArg("-dbcrashratio", 0);
    assert(!hashBlock.IsNull());

    uint256 old_tip = GetBestBlock();
    if (old_tip.IsNull()) {
        // We may be in the middle of replaying.
        std::vector<uint256> old_heads = GetHeadBlocks();
        if (old_heads.size() == 2) {
            assert(old_heads[0] == hashBlock);
            old_tip = old_heads[1];
        }
    }

    // In the first batch, mark the database as being in the middle of a
    // transition from old_tip to hashBlock.
    // A vector is used for future extensibility, as we may want to support
    // interrupting after partial writes from multiple independent reorgs.
    batch.Erase(DB_BEST_BLOCK);
    batch.Write(DB_HEAD_BLOCKS, Vector(hashBlock, old_tip));

    for (CCoinsMap::iterator it = mapCoins.begin(); it != mapCoins.end();) {
        if (it->second.flags & CCoinsCacheEntry::DIRTY) {
            CoinEntry entry(&it->first);
            if (it->second.coin.IsSpent())
                batch.Erase(entry);
            else
                batch.Write(entry, it->second.coin);
            changed++;
        }
        count++;
        CCoinsMap::iterator itOld = it++;
        mapCoins.erase(itOld);
        if (batch.SizeEstimate() > batch_size) {
            LogPrint(BCLog::COINDB, "Writing partial batch of %.2f MiB\n", batch.SizeEstimate() * (1.0 / 1048576.0));
            m_db->WriteBatch(batch);
            batch.Clear();
            if (crash_simulate) {
                static FastRandomContext rng;
                if (rng.randrange(crash_simulate) == 0) {
                    LogPrintf("Simulating a crash. Goodbye.\n");
                    _Exit(0);
                }
            }
        }
    }

    names.writeBatch(batch);

    // In the last batch, mark the database as consistent with hashBlock again.
    batch.Erase(DB_HEAD_BLOCKS);
    batch.Write(DB_BEST_BLOCK, hashBlock);

    LogPrint(BCLog::COINDB, "Writing final batch of %.2f MiB\n", batch.SizeEstimate() * (1.0 / 1048576.0));
    bool ret = m_db->WriteBatch(batch);
    LogPrint(BCLog::COINDB, "Committed %u changed transaction outputs (out of %u) to coin database...\n", (unsigned int)changed, (unsigned int)count);
    return ret;
}

size_t CCoinsViewDB::EstimateSize() const
{
    return m_db->EstimateSize(DB_COIN, (char)(DB_COIN+1));
}

CBlockTreeDB::CBlockTreeDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "blocks" / "index", nCacheSize, fMemory, fWipe) {
}

bool CBlockTreeDB::ReadBlockFileInfo(int nFile, CBlockFileInfo &info) {
    return Read(std::make_pair(DB_BLOCK_FILES, nFile), info);
}

bool CBlockTreeDB::WriteReindexing(bool fReindexing) {
    if (fReindexing)
        return Write(DB_REINDEX_FLAG, '1');
    else
        return Erase(DB_REINDEX_FLAG);
}

void CBlockTreeDB::ReadReindexing(bool &fReindexing) {
    fReindexing = Exists(DB_REINDEX_FLAG);
}

bool CBlockTreeDB::ReadLastBlockFile(int &nFile) {
    return Read(DB_LAST_BLOCK, nFile);
}

CCoinsViewCursor *CCoinsViewDB::Cursor() const
{
    CCoinsViewDBCursor *i = new CCoinsViewDBCursor(const_cast<CDBWrapper&>(*m_db).NewIterator(), GetBestBlock());
    /* It seems that there are no "const iterators" for LevelDB.  Since we
       only need read operations on it, use a const-cast to get around
       that restriction.  */
    i->pcursor->Seek(DB_COIN);
    // Cache key of first record
    if (i->pcursor->Valid()) {
        CoinEntry entry(&i->keyTmp.second);
        i->pcursor->GetKey(entry);
        i->keyTmp.first = entry.key;
    } else {
        i->keyTmp.first = 0; // Make sure Valid() and GetKey() return false
    }
    return i;
}

bool CCoinsViewDBCursor::GetKey(COutPoint &key) const
{
    // Return cached key
    if (keyTmp.first == DB_COIN) {
        key = keyTmp.second;
        return true;
    }
    return false;
}

bool CCoinsViewDBCursor::GetValue(Coin &coin) const
{
    return pcursor->GetValue(coin);
}

unsigned int CCoinsViewDBCursor::GetValueSize() const
{
    return pcursor->GetValueSize();
}

bool CCoinsViewDBCursor::Valid() const
{
    return keyTmp.first == DB_COIN;
}

void CCoinsViewDBCursor::Next()
{
    pcursor->Next();
    CoinEntry entry(&keyTmp.second);
    if (!pcursor->Valid() || !pcursor->GetKey(entry)) {
        keyTmp.first = 0; // Invalidate cached key after last record so that Valid() and GetKey() return false
    } else {
        keyTmp.first = entry.key;
    }
}

bool CBlockTreeDB::WriteBatchSync(const std::vector<std::pair<int, const CBlockFileInfo*> >& fileInfo, int nLastFile, const std::vector<const CBlockIndex*>& blockinfo) {
    CDBBatch batch(*this);
    for (std::vector<std::pair<int, const CBlockFileInfo*> >::const_iterator it=fileInfo.begin(); it != fileInfo.end(); it++) {
        batch.Write(std::make_pair(DB_BLOCK_FILES, it->first), *it->second);
    }
    batch.Write(DB_LAST_BLOCK, nLastFile);
    for (std::vector<const CBlockIndex*>::const_iterator it=blockinfo.begin(); it != blockinfo.end(); it++) {
        batch.Write(std::make_pair(DB_BLOCK_INDEX, (*it)->GetBlockHash()), CDiskBlockIndex(*it));
    }
    return WriteBatch(batch, true);
}

bool CCoinsViewDB::ValidateNameDB(ChainstateManager& chainman) const
{
    const uint256 blockHash = GetBestBlock();
    int nHeight;
    if (blockHash.IsNull())
        nHeight = 0;
    else
        nHeight = chainman.BlockIndex ().find(blockHash)->second->nHeight;

    /* It seems that there are no "const iterators" for LevelDB.  Since we
       only need read operations on it, use a const-cast to get around
       that restriction.  */
    std::unique_ptr<CDBIterator> pcursor(const_cast<CDBWrapper*>(m_db.get())->NewIterator());
    pcursor->SeekToFirst();

    /* Loop over the total database and read interesting
       things to memory.  We later use that to check
       everything against each other.  */

    std::map<valtype, unsigned> nameHeightsIndex;
    std::map<valtype, unsigned> nameHeightsData;
    std::set<valtype> namesInDB;
    std::set<valtype> namesInUTXO;
    std::set<valtype> namesWithHistory;

    for (; pcursor->Valid(); pcursor->Next())
    {
        boost::this_thread::interruption_point();
        char chType;
        if (!pcursor->GetKey(chType))
            continue;

        switch (chType)
        {
        case DB_COIN:
        {
            Coin coin;
            if (!pcursor->GetValue(coin))
                return error("%s : failed to read coin", __func__);

            if (!coin.out.IsNull())
            {
                const CNameScript nameOp(coin.out.scriptPubKey);
                if (nameOp.isNameOp() && nameOp.isAnyUpdate())
                {
                    const valtype& name = nameOp.getOpName();
                    if (namesInUTXO.count(name) > 0)
                        return error("%s : name %s duplicated in UTXO set",
                                     __func__, EncodeNameForMessage(name));
                    namesInUTXO.insert(nameOp.getOpName());
                }
            }
            break;
        }

        case DB_NAME:
        {
            std::pair<char, valtype> key;
            if (!pcursor->GetKey(key) || key.first != DB_NAME)
                return error("%s : failed to read DB_NAME key", __func__);
            const valtype& name = key.second;

            CNameData data;
            if (!pcursor->GetValue(data))
                return error("%s : failed to read name value", __func__);

            if (nameHeightsData.count(name) > 0)
                return error("%s : name %s duplicated in name index",
                             __func__, EncodeNameForMessage(name));
            nameHeightsData.insert(std::make_pair(name, data.getHeight()));
            
            /* Expiration is checked at height+1, because that matches
               how the UTXO set is cleared in ExpireNames.  */
            assert(namesInDB.count(name) == 0);
            if (!data.isExpired(nHeight + 1))
                namesInDB.insert(name);
            break;
        }

        case DB_NAME_HISTORY:
        {
            std::pair<char, valtype> key;
            if (!pcursor->GetKey(key) || key.first != DB_NAME_HISTORY)
                return error("%s : failed to read DB_NAME_HISTORY key",
                             __func__);
            const valtype& name = key.second;

            if (namesWithHistory.count(name) > 0)
                return error("%s : name %s has duplicate history",
                             __func__, EncodeNameForMessage(name));
            namesWithHistory.insert(name);
            break;
        }

        case DB_NAME_EXPIRY:
        {
            std::pair<char, CNameCache::ExpireEntry> key;
            if (!pcursor->GetKey(key) || key.first != DB_NAME_EXPIRY)
                return error("%s : failed to read DB_NAME_EXPIRY key",
                             __func__);
            const CNameCache::ExpireEntry& entry = key.second;
            const valtype& name = entry.name;

            if (nameHeightsIndex.count(name) > 0)
                return error("%s : name %s duplicated in expire idnex",
                             __func__, EncodeNameForMessage(name));

            nameHeightsIndex.insert(std::make_pair(name, entry.nHeight));
            break;
        }

        default:
            break;
        }
    }

    /* Now verify the collected data.  */

    assert (nameHeightsData.size() >= namesInDB.size());

    if (nameHeightsIndex != nameHeightsData)
        return error("%s : name height data mismatch", __func__);

    for (const auto& name : namesInDB)
        if (namesInUTXO.count(name) == 0)
            return error("%s : name '%s' in DB but not UTXO set",
                         __func__, EncodeNameForMessage(name));
    for (const auto& name : namesInUTXO)
        if (namesInDB.count(name) == 0)
            return error("%s : name '%s' in UTXO set but not DB",
                         __func__, EncodeNameForMessage(name));

    if (fNameHistory)
    {
        for (const auto& name : namesWithHistory)
            if (nameHeightsData.count(name) == 0)
                return error("%s : history entry for name '%s' not in main DB",
                             __func__, EncodeNameForMessage(name));
    } else if (!namesWithHistory.empty ())
        return error("%s : name_history entries in DB, but"
                     " -namehistory not set", __func__);

    LogPrintf("Checked name database, %u unexpired names, %u total.\n",
              namesInDB.size(), nameHeightsData.size());
    LogPrintf("Names with history: %u\n", namesWithHistory.size());

    return true;
}

void
CNameCache::writeBatch (CDBBatch& batch) const
{
  for (EntryMap::const_iterator i = entries.begin ();
       i != entries.end (); ++i)
    batch.Write (std::make_pair (DB_NAME, i->first), i->second);

  for (std::set<valtype>::const_iterator i = deleted.begin ();
       i != deleted.end (); ++i)
    batch.Erase (std::make_pair (DB_NAME, *i));

  assert (fNameHistory || history.empty ());
  for (std::map<valtype, CNameHistory>::const_iterator i = history.begin ();
       i != history.end (); ++i)
    if (i->second.empty ())
      batch.Erase (std::make_pair (DB_NAME_HISTORY, i->first));
    else
      batch.Write (std::make_pair (DB_NAME_HISTORY, i->first), i->second);

  for (std::map<ExpireEntry, bool>::const_iterator i = expireIndex.begin ();
       i != expireIndex.end (); ++i)
    if (i->second)
      batch.Write (std::make_pair (DB_NAME_EXPIRY, i->first));
    else
      batch.Erase (std::make_pair (DB_NAME_EXPIRY, i->first));
}

bool CBlockTreeDB::WriteFlag(const std::string &name, bool fValue) {
    return Write(std::make_pair(DB_FLAG, name), fValue ? '1' : '0');
}

bool CBlockTreeDB::ReadFlag(const std::string &name, bool &fValue) {
    char ch;
    if (!Read(std::make_pair(DB_FLAG, name), ch))
        return false;
    fValue = ch == '1';
    return true;
}

bool CBlockTreeDB::LoadBlockIndexGuts(const Consensus::Params& consensusParams, std::function<CBlockIndex*(const uint256&)> insertBlockIndex)
{
    std::unique_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(std::make_pair(DB_BLOCK_INDEX, uint256()));

    // Load m_block_index
    while (pcursor->Valid()) {
        if (ShutdownRequested()) return false;
        std::pair<char, uint256> key;
        if (pcursor->GetKey(key) && key.first == DB_BLOCK_INDEX) {
            CDiskBlockIndex diskindex;
            if (pcursor->GetValue(diskindex)) {
                // Construct block index object
                CBlockIndex* pindexNew = insertBlockIndex(diskindex.GetBlockHash());
                pindexNew->pprev          = insertBlockIndex(diskindex.hashPrev);
                pindexNew->nHeight        = diskindex.nHeight;
                pindexNew->nFile          = diskindex.nFile;
                pindexNew->nDataPos       = diskindex.nDataPos;
                pindexNew->nUndoPos       = diskindex.nUndoPos;
                pindexNew->nVersion       = diskindex.nVersion;
                pindexNew->hashMerkleRoot = diskindex.hashMerkleRoot;
                pindexNew->nTime          = diskindex.nTime;
                pindexNew->nBits          = diskindex.nBits;
                pindexNew->nNonce         = diskindex.nNonce;
                pindexNew->nStatus        = diskindex.nStatus;
                pindexNew->nTx            = diskindex.nTx;

                /* Bitcoin checks the PoW here.  We don't do this because
                   the CDiskBlockIndex does not contain the auxpow.
                   This check isn't important, since the data on disk should
                   already be valid and can be trusted.  */

                pcursor->Next();
            } else {
                return error("%s: failed to read value", __func__);
            }
        } else {
            break;
        }
    }

    return true;
}

namespace {

//! Legacy class to deserialize pre-pertxout database entries without reindex.
class CCoins
{
public:
    //! whether transaction is a coinbase
    bool fCoinBase;

    //! unspent transaction outputs; spent outputs are .IsNull(); spent outputs at the end of the array are dropped
    std::vector<CTxOut> vout;

    //! at which height this transaction was included in the active block chain
    int nHeight;

    //! empty constructor
    CCoins() : fCoinBase(false), vout(0), nHeight(0) { }

    template<typename Stream>
    void Unserialize(Stream &s) {
        unsigned int nCode = 0;
        // version
        unsigned int nVersionDummy;
        ::Unserialize(s, VARINT(nVersionDummy));
        // header code
        ::Unserialize(s, VARINT(nCode));
        fCoinBase = nCode & 1;
        std::vector<bool> vAvail(2, false);
        vAvail[0] = (nCode & 2) != 0;
        vAvail[1] = (nCode & 4) != 0;
        unsigned int nMaskCode = (nCode / 8) + ((nCode & 6) != 0 ? 0 : 1);
        // spentness bitmask
        while (nMaskCode > 0) {
            unsigned char chAvail = 0;
            ::Unserialize(s, chAvail);
            for (unsigned int p = 0; p < 8; p++) {
                bool f = (chAvail & (1 << p)) != 0;
                vAvail.push_back(f);
            }
            if (chAvail != 0)
                nMaskCode--;
        }
        // txouts themself
        vout.assign(vAvail.size(), CTxOut());
        for (unsigned int i = 0; i < vAvail.size(); i++) {
            if (vAvail[i])
                ::Unserialize(s, Using<TxOutCompression>(vout[i]));
        }
        // coinbase height
        ::Unserialize(s, VARINT_MODE(nHeight, VarIntMode::NONNEGATIVE_SIGNED));
    }
};

}

/** Upgrade the database from older formats.
 *
 * Currently implemented: from the per-tx utxo model (0.8..0.14.x) to per-txout.
 */
bool CCoinsViewDB::Upgrade() {
    std::unique_ptr<CDBIterator> pcursor(m_db->NewIterator());
    pcursor->Seek(std::make_pair(DB_COINS, uint256()));
    if (!pcursor->Valid()) {
        return true;
    }

    int64_t count = 0;
    LogPrintf("Upgrading utxo-set database...\n");
    LogPrintf("[0%%]..."); /* Continued */
    uiInterface.ShowProgress(_("Upgrading UTXO database").translated, 0, true);
    size_t batch_size = 1 << 24;
    CDBBatch batch(*m_db);
    int reportDone = 0;
    std::pair<unsigned char, uint256> key;
    std::pair<unsigned char, uint256> prev_key = {DB_COINS, uint256()};
    while (pcursor->Valid()) {
        if (ShutdownRequested()) {
            break;
        }
        if (pcursor->GetKey(key) && key.first == DB_COINS) {
            if (count++ % 256 == 0) {
                uint32_t high = 0x100 * *key.second.begin() + *(key.second.begin() + 1);
                int percentageDone = (int)(high * 100.0 / 65536.0 + 0.5);
                uiInterface.ShowProgress(_("Upgrading UTXO database").translated, percentageDone, true);
                if (reportDone < percentageDone/10) {
                    // report max. every 10% step
                    LogPrintf("[%d%%]...", percentageDone); /* Continued */
                    reportDone = percentageDone/10;
                }
            }
            CCoins old_coins;
            if (!pcursor->GetValue(old_coins)) {
                return error("%s: cannot parse CCoins record", __func__);
            }
            COutPoint outpoint(key.second, 0);
            for (size_t i = 0; i < old_coins.vout.size(); ++i) {
                if (!old_coins.vout[i].IsNull() && !old_coins.vout[i].scriptPubKey.IsUnspendable()) {
                    Coin newcoin(std::move(old_coins.vout[i]), old_coins.nHeight, old_coins.fCoinBase);
                    outpoint.n = i;
                    CoinEntry entry(&outpoint);
                    batch.Write(entry, newcoin);
                }
            }
            batch.Erase(key);
            if (batch.SizeEstimate() > batch_size) {
                m_db->WriteBatch(batch);
                batch.Clear();
                m_db->CompactRange(prev_key, key);
                prev_key = key;
            }
            pcursor->Next();
        } else {
            break;
        }
    }
    m_db->WriteBatch(batch);
    m_db->CompactRange({DB_COINS, uint256()}, key);
    uiInterface.ShowProgress("", 100, false);
    LogPrintf("[%s].\n", ShutdownRequested() ? "CANCELLED" : "DONE");
    return !ShutdownRequested();
}
