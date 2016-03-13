// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PRIMITIVES_PUREHEADER_H
#define BITCOIN_PRIMITIVES_PUREHEADER_H

#include "serialize.h"
#include "uint256.h"

/**
 * A block header without auxpow information.  This "intermediate step"
 * in constructing the full header is useful, because it breaks the cyclic
 * dependency between auxpow (referencing a parent block header) and
 * the block header (referencing an auxpow).  The parent block header
 * does not have auxpow itself, so it is a pure header.
 */
class CPureBlockHeader
{
public:

    /* Constants used in the legacy nVersion encoding.  */
    static const int32_t VERSION_AUXPOW = (1 << 8);
    static const int32_t VERSION_CHAIN_START = (1 << 16);

private:

    /**
     * Mask for nonce that yields the chain ID after the always-auxpow fork.
     * Since chain ID is only 16 bits, we may want to use other bits from the
     * nonce for more information in the future.
     */
    static const int32_t NONCE_CHAINID_MASK = 0xffff;

    /**
     * Block time that activates the always-auxpow hardfork.  Since this
     * fork changes the header serialisation format, we specify it on
     * a very low level and do not involve any chain parameters.  Otherwise
     * those would be needed whenever we serialise/deserialise a header.
     */
    /* FIXME: Set to 2017-01-01 for now, change later as necessary!  */
    static const int64_t ALWAYS_AUXPOW_FORK_TIME = 1483225200;

    static inline int32_t GetBaseVersion(int32_t time, int32_t ver)
    {
        if (time >= ALWAYS_AUXPOW_FORK_TIME)
          return ver;
        return ver % VERSION_AUXPOW;
    }

public:
    // header
    int32_t nVersion;
    uint256 hashPrevBlock;
    uint256 hashMerkleRoot;
    uint32_t nTime;
    uint32_t nBits;
    uint32_t nNonce;

    CPureBlockHeader()
    {
        SetNull();
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(this->nVersion);
        nVersion = this->GetBaseVersion();
        READWRITE(hashPrevBlock);
        READWRITE(hashMerkleRoot);
        READWRITE(nTime);
        READWRITE(nBits);
        READWRITE(nNonce);
    }

    void SetNull()
    {
        nVersion = 0;
        hashPrevBlock.SetNull();
        hashMerkleRoot.SetNull();
        nTime = 0;
        nBits = 0;
        nNonce = 0;
    }

    bool IsNull() const
    {
        return (nBits == 0);
    }

    uint256 GetHash() const;

    int64_t GetBlockTime() const
    {
        return (int64_t)nTime;
    }

    /**
     * Initialise a block's version and chain ID.  The block is always
     * assumed to be merge-mined, since that's what all generated blocks
     * are nowadays.
     */
    void SetVersionAndChainId(int32_t ver, int32_t chainId);

    /**
     * Extract the base version (without modifiers and chain ID).
     * @return The base version.
     */
    inline int32_t GetBaseVersion() const
    {
        return GetBaseVersion(nTime, nVersion);
    }

    /**
     * Extract the chain ID.
     * @return The chain ID encoded in the version.
     */
    inline int32_t GetChainId() const
    {
        if (AlwaysAuxpowActive ())
            return nNonce & NONCE_CHAINID_MASK;
        return nVersion / VERSION_CHAIN_START;
    }

    /**
     * Check if the auxpow flag is set in the version.
     * @return True iff this block version is marked as auxpow.
     */
    inline bool IsAuxpow() const
    {
        if (AlwaysAuxpowActive ())
            return true;
        return nVersion & VERSION_AUXPOW;
    }

    /**
     * Check whether this is a "legacy" block without chain ID.
     * @return True iff it is.
     */
    /* FIXME: Get rid of this once the chain is beyond the always-auxpow
       fork.  Then this is no longer needed.  */
    inline bool IsLegacy() const
    {
        if (AlwaysAuxpowActive ())
            return false;
        return nVersion == 1;
    }

    /**
     * Check whether the always-auxpow fork is active in this block.
     * This is made publicly available since the fork triggers also
     * other things (like the BDB lock limit).
     */
    inline bool AlwaysAuxpowActive() const
    {
        return GetBlockTime() >= ALWAYS_AUXPOW_FORK_TIME;
    }

    /* Friend CBlockIndex so that it can access the otherwise
       private logic about interpreting nVersion.  */
    friend class CBlockIndex;
};

#endif // BITCOIN_PRIMITIVES_PUREHEADER_H
