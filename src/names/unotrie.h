// Copyright (c) 2014-2015 Daniel Kraft
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef H_BITCOIN_NAMES_UNOTRIE
#define H_BITCOIN_NAMES_UNOTRIE

#include "hash.h"
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

  /** Data of the name corresponding to this node (if any).  */
  CNameData* data;

  /** Subtrie's indexed by their "next character".  */
  std::map<unsigned char, CUnoTrie*> children;

  /**
   * Release all memory stored.  This is used both from the destructor
   * and also when unserialising (to free the old state, if any).
   * Thus shared here.
   */
  void Clear ();

public:

  /**
   * Construct it with empty data and no children.
   */
  inline CUnoTrie ()
    : data(NULL), children()
  {}

  /**
   * Destruct and free all memory.
   */
  virtual ~CUnoTrie ();

  /**
   * Get root hash of this node (including the full subtree).
   * @return The node's subtree's hash.
   */
  inline uint256
  GetHash () const
  {
    return SerializeHash (*this);
  }

  /**
   * Insert the given data in the subtree at the given position.
   * If there is already data at the position, it is replaced.
   * There must not yet be data at this position.
   * @param a Start of the prefix.
   * @param b End of the prefix.
   * @param d Data of the name to put there.
   */
  template<typename Iter>
    void
    Insert (const Iter& a, const Iter& b, const CNameData& d)
  {
    if (a == b)
      {
        if (data)
          delete data;
        data = new CNameData (d);
        return;
      }

    const unsigned char nextByte = *a;
    std::map<unsigned char, CUnoTrie*>::iterator mi = children.find (nextByte);
    if (mi != children.end ())
      mi->second->Insert (a + 1, b, d);
    else
      {
        std::auto_ptr<CUnoTrie> child(new CUnoTrie ());
        child->Insert (a + 1, b, d);
        children.insert (std::make_pair (nextByte, child.release ()));
      }
  }

  /* Implement serialisation.  Note that the "get hash" serialisation
     uses the hashes of all children (as in a Merkle tree) instead
     of the fully serialised children themselves.  */

  ADD_SERIALIZE_METHODS;

  template<typename Stream, typename Operation>
    inline void SerializationOp (Stream& s, Operation ser_action,
                                 int nType, int nVersion)
  {
    if (ser_action.ForRead ())
      {
        Clear ();
        assert (!data);
        assert (children.empty ());
      }

    bool fHasData;
    if (!ser_action.ForRead ())
      fHasData = (data != NULL);
    READWRITE (fHasData);

    if (fHasData)
      READWRITE (*data);

    uint16_t sz;
    if (!ser_action.ForRead ())
      sz = children.size ();
    READWRITE (sz);

    /* We are reading, fill in entries one by one.  */
    if (ser_action.ForRead ())
      for (unsigned i = 0; i < sz; ++i)
        {
          unsigned char nextByte;
          std::auto_ptr<CUnoTrie> child(new CUnoTrie ());

          READWRITE (nextByte);
          READWRITE (*child);

          if (children.count (nextByte) > 0)
            throw std::runtime_error ("duplicate child character during"
                                      " unserialisation of CUnoTrie");
          children.insert (std::make_pair (nextByte, child.release ()));
        }

    /* We are writing, iterate over the map.  */
    else
      BOOST_FOREACH (const PAIRTYPE(unsigned char, CUnoTrie*)& child, children)
        {
          const unsigned char nextByte = child.first;
          READWRITE (nextByte);

          if (nType & SER_GETHASH)
            {
              const uint256 hash = child.second->GetHash ();
              READWRITE (hash);
            }
          else
            READWRITE (*child.second);
        }
  }

};

#endif // H_BITCOIN_NAMES_UNOTRIE
