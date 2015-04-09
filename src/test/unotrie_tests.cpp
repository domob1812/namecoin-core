// Copyright (c) 2015 Daniel Kraft
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "coins.h"
#include "main.h"
#include "names/common.h"
#include "names/unotrie.h"
#include "primitives/transaction.h"
#include "script/names.h"
#include "script/script.h"
#include "streams.h"
#include "test/test_bitcoin.h"
#include "uint256.h"

#include <boost/test/unit_test.hpp>

#include <map>
#include <string>

BOOST_FIXTURE_TEST_SUITE (unotrie_tests, TestingSetup)

/**
 * Utility function that returns a sample CNameData object to use
 * in the tests.  It can be modified by the passed "counter" value.
 * @param c Value that can be used to modify the data.
 * @return A CNameData object to use for testing.
 */
static CNameData
getTestData (unsigned c)
{
  CBitcoinAddress addr("N5e1vXUUL3KfhPyVjQZSes1qQ7eyarDbUU");
  BOOST_CHECK (addr.IsValid ());
  const CScript baseScript = GetScriptForDestination (addr.Get ());

  /* Note that the name in the script parsed by CNameData::fromScript does not
     matter.  Just use something.  */
  const valtype name = ValtypeFromString ("example-name");
  const valtype value = ValtypeFromString ("some-value");

  const CScript updateScript
    = CNameScript::buildNameUpdate (baseScript, name, value);

  CNameData res;
  res.fromScript (c, COutPoint (), CNameScript (updateScript));

  return res;
}

/* ************************************************************************** */
/* CUnoTester.  */

/**
 * This class should be subclassed for various tests.  It can then be
 * used together with testUnoUpdates, which will perform a series
 * of additions, upates and deletes on it.  The class computes the "correct"
 * root hash by a freshly built trie on each step, and allows tests to
 * compare their own results to it.
 */
class CUnoTester
{

private:

  /** Internal counter used to compute fresh name datas.  */
  unsigned counter;

  /** The computed root hash for the current step.  */
  uint256 hash;

  /** Keep track of the currently existing names.  */
  std::map<valtype, CNameData> names;

  /**
   * Update the root hash from the names map.
   */
  void updateHash ();

  /**
   * Get test data object to use.
   * @return Test data object with incremented counter.
   */
  inline CNameData
  getData ()
  {
    ++counter;
    return getTestData (counter);
  }

public:

  /**
   * Build tester initially.
   */
  inline CUnoTester ()
    : counter(0), hash(), names()
  {
    updateHash ();
  }

  /**
   * Get current hash.
   * @return Current hash.
   */
  inline const uint256&
  GetHash () const
  {
    return hash;
  }

  /* These will be called by testUnoUpdates.  */
  void fullAdd (const std::string& name);
  void fullUpdate (const std::string& name);
  void fullDelete (const std::string& name);

protected:

  /* These must be implemented by the subclasses.  */
  virtual void internalUpdate (const valtype& name, const CNameData& data) = 0;
  virtual void internalDelete (const valtype& name) = 0;

  /* Default implementation of "add" is to call "update".  */
  virtual void internalAdd (const valtype& name, const CNameData& data);
  
  /* Called after each change, can be used to check the state.  */
  virtual void checkState () = 0;

};

void
CUnoTester::updateHash ()
{
  CUnoTrie trie;

  for (std::map<valtype, CNameData>::const_iterator i = names.begin ();
       i != names.end (); ++i)
    trie.Set (i->first.begin (), i->first.end (), i->second, true);

  BOOST_CHECK (trie.Check (true));
  hash = trie.GetHash ();
}

void
CUnoTester::fullAdd (const std::string& name)
{
  const valtype vchName = ValtypeFromString (name);
  const CNameData data = getData ();

  BOOST_CHECK (names.count (vchName) == 0);
  names.insert (std::make_pair (vchName, data));
  updateHash ();

  internalAdd (vchName, data);
  checkState ();
}

void
CUnoTester::fullUpdate (const std::string& name)
{
  const valtype vchName = ValtypeFromString (name);
  const CNameData data = getData ();

  BOOST_CHECK (names.count (vchName) == 1);
  names[vchName] = data;
  updateHash ();

  internalUpdate (vchName, data);
  checkState ();
}

void
CUnoTester::fullDelete (const std::string& name)
{
  const valtype vchName = ValtypeFromString (name);

  BOOST_CHECK (names.count (vchName) == 1);
  names.erase (vchName);
  updateHash ();

  internalDelete (vchName);
  checkState ();
}

void
CUnoTester::internalAdd (const valtype& name, const CNameData& data)
{
  internalUpdate (name, data);
}

void
CUnoTester::checkState ()
{
  // Do nothing in the default implementation.
}

/**
 * Call the CUnoTester with some sequence of updates.
 * @param tester The CUnoTester instance to use.
 */
static void
testUnoUpdates (CUnoTester& tester)
{
  tester.fullAdd ("foobar");
  tester.fullUpdate ("foobar");
  tester.fullDelete ("foobar");

  tester.fullAdd ("ab");
  tester.fullAdd ("abcd");
  tester.fullUpdate ("abcd");
  tester.fullDelete ("abcd");
  tester.fullAdd ("abcd");
  tester.fullAdd ("abef");
  tester.fullDelete ("abef");
  tester.fullDelete ("ab");

  for (int i = 0; i <= 5; ++i)
    tester.fullAdd (ValtypeToString (valtype (i, 'x')));
  for (int i = 0; i <= 5; ++i)
    tester.fullDelete (ValtypeToString (valtype (i, 'x')));

  for (int i = 0; i <= 5; ++i)
    tester.fullAdd (ValtypeToString (valtype (i, 'x')));
  for (int i = 5; i >= 0; --i)
    tester.fullDelete (ValtypeToString (valtype (i, 'x')));

  for (int i = 5; i >= 0; --i)
    tester.fullAdd (ValtypeToString (valtype (i, 'x')));
  for (int i = 5; i >= 0; --i)
    tester.fullDelete (ValtypeToString (valtype (i, 'x')));

  for (int i = 5; i >= 0; --i)
    tester.fullAdd (ValtypeToString (valtype (i, 'x')));
  for (int i = 0; i <= 5; ++i)
    tester.fullDelete (ValtypeToString (valtype (i, 'x')));
}

/* ************************************************************************** */

/**
 * Tester that builds an expanded and unexpanded trie, checks the hashes,
 * checks the tries for correctness and serialises / unserialises them.
 */
class BuildTriesTester : public CUnoTester
{

private:

  /** Expanded trie being built.  */
  CUnoTrie expanded;
  /** Unexpanded trie being built.  */
  CUnoTrie unexpanded;

public:

  /**
   * Construct with empty tries.
   */
  inline BuildTriesTester ()
    : CUnoTester(), expanded(), unexpanded()
  {}

protected:

  /* Implement required update methods.  */
  virtual void internalUpdate (const valtype& name, const CNameData& data);
  virtual void internalDelete (const valtype& name);

  virtual void checkState ();

};

void
BuildTriesTester::checkState ()
{
  CUnoTester::checkState ();

  BOOST_CHECK (expanded.Check (true));
  BOOST_CHECK (unexpanded.Check (false));
  BOOST_CHECK (expanded.GetHash () == GetHash ());
  BOOST_CHECK (unexpanded.GetHash () == GetHash ());

  CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
  CUnoTrie trie;
  stream << expanded;
  stream >> trie;
  BOOST_CHECK (trie.Check (true));
  BOOST_CHECK (trie.GetHash () == GetHash ());

  stream.clear ();
  stream << unexpanded;
  stream >> trie;
  BOOST_CHECK (trie.Check (false));
  BOOST_CHECK (trie.GetHash () == GetHash ());
}

void
BuildTriesTester::internalUpdate (const valtype& name, const CNameData& data)
{
  expanded.Set (name.begin (), name.end (), data, true);
  unexpanded.Set (name.begin (), name.end (), data, false);
}

void
BuildTriesTester::internalDelete (const valtype& name)
{
  expanded.Delete (name.begin (), name.end (), true);
  unexpanded.Delete (name.begin (), name.end (), false);
}

BOOST_AUTO_TEST_CASE (unotrie_building)
{
  BuildTriesTester tester;
  testUnoUpdates (tester);
}

/* ************************************************************************** */

/**
 * Tester that performs updates through a CCoinsViewCache and checks that
 * the associated UNO tries are updated correctly.  It only builds unexpanded
 * tries.  This is what the real code does, and basic trie building
 * is already checked by unotrie_building anyway.
 */
class CoinsViewCacheTester : public CUnoTester
{

private:

  /** Base view, should be backed by a database (i. e., pcoinsTip).  */
  CCoinsViewCache& base;
  /** Layered view that gets changes 'flushed' to it.  */
  CCoinsViewCache layer1;
  /** Layered view that gets changes applied directly.  */
  CCoinsViewCache layer2;

  /**
   * Perform the verifications after any change.
   */
  void test ();

public:

  /**
   * Construct with given base view.
   * @param b The base view to use.
   */
  explicit inline CoinsViewCacheTester (CCoinsViewCache& b)
    : CUnoTester(), base(b), layer1(&base), layer2(&layer1)
  {
    base.Flush ();
    layer1.BuildUnoTrie (false);
    layer2.BuildUnoTrie (false);
  }

protected:

  /* Implement required update methods.  */
  virtual void internalUpdate (const valtype& name, const CNameData& data);
  virtual void internalDelete (const valtype& name);

  virtual void checkState ();

};

void
CoinsViewCacheTester::checkState ()
{
  CUnoTester::checkState ();

  layer2.Flush ();
  BOOST_CHECK (layer2.CheckUnoTrie ());
  BOOST_CHECK (layer1.CheckUnoTrie ());
  BOOST_CHECK (layer2.GetUnoTrie ().GetHash () == GetHash ());
  BOOST_CHECK (layer1.GetUnoTrie ().GetHash () == GetHash ());

  layer1.Flush ();
  base.Flush ();
  BOOST_CHECK (!base.HasUnoTrie ());
  base.BuildUnoTrie (false);
  BOOST_CHECK (base.CheckUnoTrie ());
  BOOST_CHECK (base.GetUnoTrie ().GetHash () == GetHash ());
  base.ClearUnoTrie ();
  BOOST_CHECK (!base.HasUnoTrie ());
}

void
CoinsViewCacheTester::internalUpdate (const valtype& name,
                                      const CNameData& data)
{
  layer2.SetName (name, data, false);
}

void
CoinsViewCacheTester::internalDelete (const valtype& name)
{
  layer2.DeleteName (name);
}

BOOST_AUTO_TEST_CASE (unotrie_coinsviewcache)
{
  CoinsViewCacheTester tester(*pcoinsTip);
  testUnoUpdates (tester);
}

/* ************************************************************************** */

BOOST_AUTO_TEST_SUITE_END ()
