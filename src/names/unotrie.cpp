// Copyright (c) 2015 Daniel Kraft
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "names/unotrie.h"

#include "hash.h"

#include <algorithm>
#include <utility>

/* ************************************************************************** */
/* CUnoTrie.  */

CUnoTrie::~CUnoTrie ()
{
  Clear ();
}

void
CUnoTrie::Clear ()
{
  if (data)
    delete data;
  data = NULL;

  BOOST_FOREACH(PAIRTYPE(unsigned char, CUnoTrie*) child, children)
    delete child.second;
  children.clear ();
}

uint256
CUnoTrie::GetHash () const
{
  std::map<unsigned char, uint256> childHashes;
  BOOST_FOREACH(PAIRTYPE(unsigned char, const CUnoTrie*) child, children)
    childHashes.insert (std::make_pair (child.first, child.second->GetHash ()));

  /* Compute base hash.  This is the hash without the prefix.  I. e.,
     the hash of the "lowest" trie node, which actually holds all the
     data and the children present in the object.  */
  uint256 res;
  {
    CHashWriter hasher(SER_GETHASH, PROTOCOL_VERSION);
    uint8_t flags = 0;
    if (data)
      flags |= FLAG_HASDATA;
    hasher << flags;
    if (data)
      hasher << *data;
    hasher << childHashes;
    res = hasher.GetHash ();
  }

  /* Follow up the prefix, if there is any.  */
  valtype::const_reverse_iterator i;
  for (i = prefix.rbegin (); i != prefix.rend (); ++i)
    {
      CHashWriter hasher(SER_GETHASH, PROTOCOL_VERSION);
      hasher << false;

      childHashes.clear ();
      childHashes.insert (std::make_pair (*i, res));
      hasher << childHashes;

      res = hasher.GetHash ();
    }

  return res;
}

void
CUnoTrie::Set (valtype::const_iterator a, const valtype::const_iterator& b,
               const CNameData& d, bool expanded)
{
  /* Follow the prefix as far as possible.  */
  valtype::const_iterator i = prefix.begin ();
  while (i != prefix.end () && a != b && *i == *a)
    {
      ++i;
      ++a;
    }

  /* If we have not yet reached the end of the prefix, we have to split
     it and insert a new intermediate node.  In fact, the current node
     is turned into the intermediate one (so that upstream pointers
     are preserved).  */
  if (i != prefix.end ())
    {
      std::auto_ptr<CUnoTrie> newChild;
      newChild.reset (new CUnoTrie (i + 1, prefix.end ()));

      assert (!newChild->data);
      std::swap (data, newChild->data);

      assert (newChild->children.empty ());
      newChild->children.insert (std::make_pair (*i, newChild.get ()));
      children.swap (newChild->children);
      newChild.release ();

      const valtype::size_type newSize = i - prefix.begin ();
      assert (newSize < prefix.size ());
      prefix.resize (newSize);

      // Fall through.
    }

  /* Now insert into the child array.  */
  assert (i == prefix.end ());
  if (a == b)
    {
      if (data)
        delete data;
      data = new CNameData (d);
    }
  else
    {
      const unsigned char nextByte = *a;
      std::map<unsigned char, CUnoTrie*>::iterator mi;
      mi = children.find (nextByte);

      ++a;
      if (mi == children.end ())
        {
          std::auto_ptr<CUnoTrie> newChild;
          if (expanded)
            {
              newChild.reset (new CUnoTrie ());
              newChild->Set (a, b, d, expanded);
            }
          else
            newChild.reset (new CUnoTrie (a, b, new CNameData (d)));
          children.insert (std::make_pair (nextByte, newChild.release ()));
        }
      else
        mi->second->Set (a, b, d, expanded);
    }
}
