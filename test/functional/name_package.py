#!/usr/bin/env python3
# Copyright (c) 2026 The Namecoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Test that duplicate name operations in a package cannot abort the node."""

from decimal import Decimal
import math

from test_framework.messages import COIN, msg_tx, tx_from_hex
from test_framework.p2p import P2PInterface
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal
from test_framework.wallet import MiniWallet

LOCKED_SATS = COIN // 100


class NamePackageTest (BitcoinTestFramework):

  def set_test_params (self):
    self.setup_clean_chain = True
    self.num_nodes = 2
    # Use the real Namecoin relay fee so that the zero-fee parent is only
    # accepted as part of a package.
    self.extra_args = [
      ["-minrelaytxfee=0.001", "-mintxfee=0.001"],
      ["-minrelaytxfee=0.001", "-mintxfee=0.001", "-v2transport=0"],
    ]

  @staticmethod
  def to_sats (value):
    return int (Decimal (str (value)) * COIN)

  @staticmethod
  def txid_hex (tx):
    """Return the txid as hex, compatible with older test frameworks."""

    if hasattr (tx, "txid_hex"):
      return tx.txid_hex
    return tx.rehash ()

  def make_commitment (self, name, salt):
    """Create and mine a NAME_NEW commitment, returning its UTXO."""

    node = self.nodes[0]
    coin = self.wallet.get_utxo (confirmed_only=True)
    base = self.wallet.create_self_transfer (utxo_to_spend=coin,
                                             fee_rate=Decimal ("0.002"))
    named = node.namerawtransaction (base["hex"], 0,
                                     {"op": "name_new", "name": name,
                                      "rand": salt})
    txid = node.sendrawtransaction (hexstring=named["hex"], maxfeerate=0)
    decoded = node.decoderawtransaction (named["hex"])

    return {"txid": txid, "vout": 0,
            "value": Decimal (str (decoded["vout"][0]["value"]))}

  def make_name_tx (self, inputs, name_op, fee=0):
    """Build a Namecoin tx spending the given inputs and carrying name_op."""

    total = sum (self.to_sats (coin["value"]) for coin in inputs)
    base = self.wallet.create_self_transfer_multi (
      utxos_to_spend=inputs, num_outputs=2, fee_per_output=0)["tx"]
    base.vout[0].nValue = LOCKED_SATS
    base.vout[1].nValue = total - LOCKED_SATS - fee
    named = self.nodes[0].namerawtransaction (base.serialize ().hex (), 0,
                                              name_op)

    return tx_from_hex (named["hex"])

  def make_firstupdate_package (self, first, second, first_name, second_name,
                                first_salt, second_salt, extra_parent_inputs=None,
                                parent_fee=0, child_fee=None):
    """Build a parent and a fee-paying child registering names."""

    op = {"op": "name_firstupdate", "value": "package test"}
    parent_inputs = [first] + (extra_parent_inputs or [])
    parent = self.make_name_tx (
      parent_inputs, {**op, "name": first_name, "rand": first_salt},
      fee=parent_fee)
    parent_change = {
      "txid": self.txid_hex (parent),
      "vout": 1,
      "value": Decimal (parent.vout[1].nValue) / COIN,
    }
    child = self.make_name_tx (
      [second, parent_change], {**op, "name": second_name, "rand": second_salt})

    if child_fee is None:
      child_fee = math.ceil ((parent.get_vsize () + child.get_vsize ()) * 200)
    child.vout[1].nValue -= child_fee

    return parent, child

  def make_name_new_package (self, name, salt, parent_inputs, parent_fee=0,
                             child_fee=None):
    """Build a parent and a fee-paying child with the same NAME_NEW hash."""

    op = {"op": "name_new", "name": name, "rand": salt}
    parent = self.make_name_tx (parent_inputs, op, fee=parent_fee)
    parent_change = {
      "txid": self.txid_hex (parent),
      "vout": 1,
      "value": Decimal (parent.vout[1].nValue) / COIN,
    }
    child = self.make_name_tx ([parent_change], op)

    if child_fee is None:
      child_fee = math.ceil ((parent.get_vsize () + child.get_vsize ()) * 200)
    child.vout[1].nValue -= child_fee

    return parent, child

  def send_as_package (self, peer, parent, child):
    """Send the child as an orphan and then the parent to trigger package evaluation."""

    peer.send_and_ping (msg_tx (child))
    peer.send_and_ping (msg_tx (parent))

  def assert_rejected_without_crash (self, peer, parent, child, label,
                                    expected_mempool):
    victim = self.nodes[1]
    height = victim.getblockcount ()
    assert_equal (victim.getrawmempool (), expected_mempool)

    self.send_as_package (peer, parent, child)

    assert victim.process.poll () is None, \
        "%s: namecoind aborted on duplicate-name package" % label
    assert_equal (victim.getrawmempool (), expected_mempool)
    assert_equal (victim.getblockcount (), height)
    assert_equal (victim.listbanned (), [])
    self.log.info ("%s: duplicate-name package rejected, node responsive", label)

  def run_test (self):
    self.wallet = MiniWallet (self.nodes[0])
    self.wallet.generate (110, called_by_framework=True)
    self.sync_all ()

    first_name = "d/package-duplicate"
    first_salt = "01" * 20
    second_salt = "02" * 20

    first = self.make_commitment (first_name, first_salt)
    second = self.make_commitment (first_name, second_salt)
    control_first = self.make_commitment ("d/package-control-parent", first_salt)
    control_second = self.make_commitment ("d/package-control-child", second_salt)

    # Confirm the commitments and give them enough depth for NAME_FIRSTUPDATE.
    self.wallet.generate (1, called_by_framework=True)
    self.wallet.generate (12, called_by_framework=True)
    self.sync_all ()

    victim = self.nodes[1]
    peer = victim.add_p2p_connection (P2PInterface ())

    # Package RBF requires the parent to be reconsiderable on 30.x, because
    # non-TRUC transactions must individually pay the minimum relay fee.
    funding = self.wallet.get_utxo (confirmed_only=True)
    conflict = self.wallet.create_self_transfer (
      utxo_to_spend=funding, fee_rate=Decimal ("0.01"))
    conflict_txid = victim.sendrawtransaction (
      hexstring=conflict["hex"], maxfeerate=0)
    assert_equal (victim.getrawmempool (), [conflict_txid])

    parent_fee = 30_000
    child_fee = 2_000_000
    expected_mempool = [conflict_txid]

    self.log.info ("Test duplicate NAME_FIRSTUPDATE names in a package")
    parent, child = self.make_firstupdate_package (
      first, second, first_name, first_name, first_salt, second_salt,
      extra_parent_inputs=[funding], parent_fee=parent_fee,
      child_fee=child_fee)
    self.assert_rejected_without_crash (
      peer, parent, child, "duplicate firstupdate", expected_mempool)

    self.log.info ("Test duplicate NAME_NEW hashes in a package")
    parent, child = self.make_name_new_package (
      "d/package-duplicate-new", "03" * 20, [funding],
      parent_fee=parent_fee, child_fee=child_fee)
    self.assert_rejected_without_crash (
      peer, parent, child, "duplicate name_new", expected_mempool)

    self.log.info ("Test that a package registering distinct names is accepted")
    parent, child = self.make_firstupdate_package (
      control_first, control_second, "d/package-control-parent",
      "d/package-control-child", first_salt, second_salt,
      extra_parent_inputs=[funding], parent_fee=parent_fee,
      child_fee=child_fee)

    self.send_as_package (peer, parent, child)
    txids = {self.txid_hex (parent), self.txid_hex (child)}
    self.wait_until (
      lambda: txids.issubset (set (victim.getrawmempool ())), timeout=10)
    assert_equal (txids.issubset (set (victim.getrawmempool ())), True)
    assert_equal (conflict_txid in victim.getrawmempool (), False)
    self.log.info ("Distinct-name package accepted and replaced conflict")


if __name__ == '__main__':
  NamePackageTest (__file__).main ()
