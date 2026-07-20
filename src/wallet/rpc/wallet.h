// Copyright (c) 2016-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_RPC_WALLET_H
#define BITCOIN_WALLET_RPC_WALLET_H

#include <span.h>
#include <univalue.h>
#include <wallet/walletutil.h>

#include <vector>

class CRPCCommand;
class CTxIn;

namespace wallet {

class CCoinControl;
class CRecipient;
class CWallet;

std::span<const CRPCCommand> GetWalletRPCCommands();

/* These are private to rpcwallet.cpp upstream, but are used also from
   rpcnames.cpp in Namecoin.  */
UniValue SendMoney(CWallet& wallet, const CCoinControl& coin_control,
                   const CTxIn* withInput,
                   std::vector<CRecipient>& recipients,
                   std::optional<std::string> comment,
                   std::optional<std::string> comment_to,
                   bool verbose);
void SetFeeEstimateMode(const CWallet& wallet, CCoinControl& cc, const UniValue& conf_target, const UniValue& estimate_mode, const UniValue& fee_rate, bool override_min_fee);

} // namespace wallet

#endif // BITCOIN_WALLET_RPC_WALLET_H
