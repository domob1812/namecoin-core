#!/usr/bin/env python3
# Copyright (c) 2016 Daniel Kraft
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Test the always-auxpow fork transition.

from test_framework import auxpow
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *

class AlwaysAuxpowTest (BitcoinTestFramework):

  def run_test (self):
    # FIXME: Set final value here.
    forktime = 1483225200

    # Set mock times of nodes to be around the fork time.
    self.nodes[0].setmocktime (forktime - 1)
    self.nodes[1].setmocktime (forktime)
    self.nodes[2].setmocktime (forktime)
    self.nodes[3].setmocktime (forktime)

    # Verify mining some blocks.
    self.mineAndVerify (0, 1, False)
    self.mineAndVerify (1, 0, True)
    self.mineAndVerify (0, 1, False)

    # Do the same with getauxblock mined blocks.
    blkhash = auxpow.mineAuxpowBlock (self.nodes[0])
    self.sync_all ()
    self.verify (self.nodes[1].getblock (blkhash), False)
    blkhash = auxpow.mineAuxpowBlock (self.nodes[1])
    self.sync_all ()
    self.verify (self.nodes[0].getblock (blkhash), True)

    # Update mock time.
    self.nodes[0].setmocktime (forktime + 1)
    self.mineAndVerify (0, 1, True)
    self.mineAndVerify (1, 0, True)

    # Mine some more blocks, just to be sure.
    for i in range (10):
      self.nodes[0].setmocktime (forktime + 4 * i)
      self.nodes[1].setmocktime (forktime + 4 * i + 1)
      self.nodes[2].setmocktime (forktime + 4 * i + 2)
      self.nodes[3].setmocktime (forktime + 4 * i + 3)
      self.mineAndVerify (0, 1, True)
      self.mineAndVerify (1, 2, True)
      self.mineAndVerify (2, 3, True)
      blkhash = auxpow.mineAuxpowBlock (self.nodes[3])
      self.sync_all ()
      self.verify (self.nodes[0].getblock (blkhash), True)

  def mineAndVerify (self, indMine, indCheck, shouldBeForked):
    """
    Mine a block on node indMine and verify that it either has the
    characteristics before or after the fork in the block.  Node indCheck
    is used to retrieve block data afterwards.
    """

    blkhash = self.nodes[indMine].generate (1)
    self.sync_all ()
    assert_equal (len (blkhash), 1)
    blkhash = blkhash[0]
    blk = self.nodes[indCheck].getblock (blkhash)

    self.verify (blk, shouldBeForked)

  def verify (self, blk, shouldBeForked):
    """
    Verify if the given blk (as JSON data) satisfies the pre- or
    postfork conditions.
    """

    if shouldBeForked:
      assert blk['version'] < (1 << 8)
      assert_equal (blk['nonce'], 1)
    else:
      assert blk['version'] > (1 << 16)
      assert_equal (blk['version'] >> 16, 1)

if __name__ == '__main__':
  AlwaysAuxpowTest ().main ()
