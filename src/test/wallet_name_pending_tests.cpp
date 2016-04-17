// Copyright (c) 2016 B.X. Roberts
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpc/server.h"
#include "names/common.h"
#include "wallet/wallet.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

#include <univalue.h>

using namespace std;

extern CWallet* pwalletMain;

BOOST_FIXTURE_TEST_SUITE(wallet_name_pending_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(wallet_name_pending_tests)
{
    string nameGood = "test/name";
    string nameBad = "test/baddata";
    string txid = "9f73e1dfa3cbae23d008307e42e72beb8c010546ea2a7b9ff32619676a9c64a6";
    string rand = "092abbca8a938103abcc";
    string data = "{\"foo\": \"bar\"}";

    UniValue uniNameUpdateData(UniValue::VOBJ);
    uniNameUpdateData.pushKV ("txid", txid);
    uniNameUpdateData.pushKV ("rand", rand);
    uniNameUpdateData.pushKV ("data", data);

    // this gets written to wallet for pending name_firstupdate
    std::string saveData = uniNameUpdateData.write();
    // and some bad data to ensure we dont segfault
    std::string badData = "flksjf984j*#)(QUFD039kjdc0e9wjf8{})";

    // ensure pending names is blank to start
    BOOST_CHECK(pendingNameFirstUpdate.size() == 0);

    // write a valid pending name_update to wallet
    {
        LOCK(pwalletMain->cs_wallet);
        CWalletDB walletdb(pwalletMain->strWalletFile);
        BOOST_CHECK(walletdb.WriteNameFirstUpdate(nameGood, saveData));

        // load the wallet and see if we get our pending name loaded
        BOOST_CHECK_NO_THROW(walletdb.LoadWallet(pwalletMain));
        // make sure we've added our pending name
        BOOST_CHECK(pendingNameFirstUpdate.size() == 1);
        BOOST_CHECK(pendingNameFirstUpdate.find(nameGood) != pendingNameFirstUpdate.end());

        // put a bad name pending to the wallet
        BOOST_CHECK(walletdb.WriteNameFirstUpdate(nameBad, badData));

        // load the wallet and ensure we don't segfault on the bad data
        BOOST_CHECK_NO_THROW(walletdb.LoadWallet(pwalletMain));
        // make sure we dont have this bad pending in memory
        BOOST_CHECK(pendingNameFirstUpdate.size() == 1);
        BOOST_CHECK(pendingNameFirstUpdate.find(nameBad) == pendingNameFirstUpdate.end());

        // test removing the names
        BOOST_CHECK(walletdb.EraseNameFirstUpdate(nameGood));
        BOOST_CHECK(walletdb.EraseNameFirstUpdate(nameBad));
    }
}

BOOST_AUTO_TEST_SUITE_END()
