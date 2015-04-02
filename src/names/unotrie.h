// Copyright (c) 2015 Daniel Kraft
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef H_BITCOIN_NAMES_UNOTRIE
#define H_BITCOIN_NAMES_UNOTRIE

#include "names/common.h"
#include "serialize.h"
#include "uint256.h"
#include "utilstrencodings.h"

#include <boost/foreach.hpp>

#include <map>
#include <memory>

/* ************************************************************************** */
/* CUnoTrie.  */

/**
 * Node / subtree of a "trie" data structure that contains all current
 * name outputs.  In other words, it represents the full name
 * database in a deterministic way.  It allows to construct
 * a commitment of the name database in block headers, which can
 * in turn be used to verify that a name entry or prefixed subtree
 * is correct.
 */
class CUnoTrie
{

private:

  /**
   * Additional bytes to add along the edge from this node to the
   * first character in the parent's map.
   */
  valtype prefix;

  /** Data of the name corresponding to this node (if any).  */
  CNameData* data;

  /** Subtrie's indexed by their "next character".  */
  std::map<unsigned char, CUnoTrie*> children;

  /**
   * Construct with given prefix range and data.  This is used
   * when inserting nodes into the trie.
   * @param a Prefix start.
   * @param b Prefix end.
   * @param d Data to insert (if any).
   */
  inline CUnoTrie (const valtype::const_iterator& a,
                   const valtype::const_iterator& b, CNameData* d = NULL)
    : prefix(a, b), data(d), children()
  {}

  /**
   * Clear all memory.  This is used for unserialisation and for the destructor,
   * thus it is shared here.
   */
  void Clear ();

public:

  /**
   * Construct it with empty data and no children.
   */
  inline CUnoTrie ()
    : prefix(), data(NULL), children()
  {}

  /**
   * Destruct and free all memory.
   */
  virtual ~CUnoTrie ();

  /**
   * Get root hash of this node (including the full subtree).  This also
   * includes the prefix.  I. e., the hash is actually of the node that
   * would correspond to the "parent" at the beginning of the prefix.
   *
   * Note that the hash itself is computed as if there were no prefixes
   * and instead an ordinary trie was used.  This is done to make the
   * consensus rules simple to implement in alternative situations.
   *
   * @return The node's subtree's hash.
   */
  uint256 GetHash () const;

  /**
   * Insert the given data in the subtree at the given position.
   * If there is already data at the position, it is replaced.
   * @param a Start of the prefix.
   * @param b End of the prefix.
   * @param d Data of the name to put there.
   */
  void Insert (valtype::const_iterator a, const valtype::const_iterator& b,
               const CNameData& d);

  /* Implement serialisation.  This is *not* used for hashing!  Hashing
     is done by GetHash in an ad-hoc fashion, since it does resolve
     the prefix explicitly.  */

  template<typename Stream>
    void
    Serialize (Stream& s, int nType, int nVersion) const
  {
    assert (!(nType & SER_GETHASH));

    s << prefix << static_cast<bool> (data != NULL);
    if (data)
      s << *data;

    WriteCompactSize (s, children.size ());
    BOOST_FOREACH (const PAIRTYPE(unsigned char, CUnoTrie*)& child, children)
      s << child.first << *child.second;
  }

  template<typename Stream>
    void
    Unserialize (Stream& s, int nType, int nVersion)
  {
    Clear ();
    s >> prefix;

    bool fHasData;
    s >> fHasData;
    assert (!data);
    if (fHasData)
      {
        data = new CNameData ();
        s >> *data;
      }

    const uint64_t sz = ReadCompactSize (s);
    assert (children.empty ());
    for (unsigned i = 0; i < sz; ++i)
      {
        unsigned char nextByte;
        std::auto_ptr<CUnoTrie> child(new CUnoTrie ());
        s >> nextByte >> *child;

        if (children.count (nextByte) > 0)
          throw std::runtime_error ("duplicate child character during"
                                    " unserialisation of CUnoTrie");
        children.insert (std::make_pair (nextByte, child.release ()));
      }
  }

};

#endif // H_BITCOIN_NAMES_UNOTRIE
