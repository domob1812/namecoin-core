#!/usr/bin/env python3
# Copyright (c) 2018 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# RPC test for the "ismine" field in the various name RPCs.

from test_framework.names import NameTestFramework
from test_framework.util import *

class NameIsMineTest (NameTestFramework):

  def set_test_params (self):
    # We only use node 1 for the test, but need that (and not node 0) because
    # it has -namehistory set in the cached chain.
    self.setup_name_test ([[], ['-namehistory']])

  def verifyExpectedIsMineInList (self, arr):
    """
    Goes through the array of name info, as returned by name_scan or name_list.
    It verifies that exactly the two expected names are there, and the
    ismine field matches the expectation for them.
    """

    assert_equal (len (arr), 2)

    for n in arr:
      if n['name'] == "d/a":
        assert_equal (n['ismine'], True)
      elif n['name'] == "d/b":
        assert_equal (n['ismine'], False)
      else:
        raise AssertionError ("Unexpected name in array: %s" % n['name'])

  def run_test (self):
    self.node = self.nodes[1]

    # Register two names.  One of them is then sent to an address not owned
    # by the node and one is updated into a pending operation.  That then
    # gives us the basic setup to test the "ismine" field in all the
    # circumstances.
    newA = self.node.name_new ("d/a")
    newB = self.node.name_new ("d/b")
    self.node.generate (10)
    self.firstupdateName (1, "d/a", newA, "value")
    self.firstupdateName (1, "d/b", newB, "value")
    self.node.generate (5)
    otherAddr = self.nodes[0].getnewaddress ()
    self.node.name_update ("d/b", "new value", {"destAddress": otherAddr})
    self.node.generate (1)
    self.node.name_update ("d/a", "new value")

    # name_show
    assert_equal (self.node.name_show ("d/a")['ismine'], True)
    assert_equal (self.node.name_show ("d/b")['ismine'], False)

    # name_history
    hist = self.node.name_history ("d/a")
    assert_equal (len (hist), 1)
    assert_equal (hist[0]['ismine'], True)
    hist = self.node.name_history ("d/b")
    assert_equal (len (hist), 2)
    assert_equal (hist[0]['ismine'], True)
    assert_equal (hist[1]['ismine'], False)

    # name_pending
    pending = self.node.name_pending ()
    assert_equal (len (pending), 1)
    p = pending[0]
    assert_equal (p['name'], "d/a")
    assert_equal (p['ismine'], True)

    # name_scan, name_filter, name_list
    self.verifyExpectedIsMineInList (self.node.name_scan ())
    self.verifyExpectedIsMineInList (self.node.name_filter ())
    self.verifyExpectedIsMineInList (self.node.name_list ())

if __name__ == '__main__':
  NameIsMineTest ().main ()
