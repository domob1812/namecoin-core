// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "primitives/pureheader.h"

#include "hash.h"
#include "utilstrencodings.h"

uint256 CPureBlockHeader::GetHash() const
{
    return SerializeHash(*this);
}

void CPureBlockHeader::SetVersionAndChainId(int32_t ver, int32_t chainId)
{
  if (!AlwaysAuxpowActive ())
    {
      assert (ver >= 0 && ver < VERSION_AUXPOW);
      nVersion = ver
                  | (chainId * VERSION_CHAIN_START)
                  | VERSION_AUXPOW;
    }
  else
    {
      assert (chainId >= 0 && chainId <= NONCE_CHAINID_MASK);
      nVersion = ver;
      nNonce = chainId;
    }
}
