// Copyright (c) 2015 Daniel Kraft
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "names/unotrie.h"

#include "hash.h"
#include "util.h"

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

bool
CUnoTrie::Delete (valtype::const_iterator a, const valtype::const_iterator& b,
                  bool expanded)
{
  /* Follow the prefix as far as possible.  */
  valtype::const_iterator i = prefix.begin ();
  while (i != prefix.end () && a != b && *i == *a)
    {
      ++i;
      ++a;
    }

  /* If we have not reached the prefix' end, this means that
     the requested element is not in the trie.  */
  if (i != prefix.end ())
    return error ("%s: element to delete not in the trie", __func__);

  /* Handle the case that we have reached the final node.  */
  if (a == b)
    {
      if (!data)
        return error ("%s: element to delete not in the trie", __func__);

      delete data;
      data = NULL;

      /* If this node remains as a "pure edge" and we want a non-expanded
         trie, strip it out.  */
      if (!expanded && IsPureEdge ())
        {
          const unsigned char nextByte = children.begin ()->first;
          std::auto_ptr<CUnoTrie> child(children.begin ()->second);
          assert (children.size () == 1);
          children.clear ();

          assert (!data);
          std::swap (data, child->data);

          prefix.push_back (nextByte);
          prefix.insert (prefix.end (), child->prefix.begin (),
                         child->prefix.end ());

          children.swap (child->children);
          assert (child->children.empty ());
          // child is deallocated on going out of scope.

          assert (!IsPureEdge ());
        }

      return true;
    }

  /* Recurse on the subtree.  Note that we have to remove childs
     that become "empty leaf" nodes by the process.  */

  const unsigned char nextByte = *a;
  std::map<unsigned char, CUnoTrie*>::iterator mi;
  mi = children.find (nextByte);

  ++a;
  if (mi == children.end ())
    return error ("%s: element to delete not in the trie", __func__);

  if (!mi->second->Delete (a, b, expanded))
    return false;

  if (mi->second->IsEmptyLeaf ())
    {
      delete mi->second;
      children.erase (mi);
    }

  return true;
}

bool
CUnoTrie::Check (bool root, bool expanded) const
{
  /* The root node can be both an empty leaf and a pure edge.  It never gets
     a prefix.  If childs are added, they may be added as "pure edge" childs
     because it has a special role.  */
  if (!root)
    {
      if (IsEmptyLeaf ())
        return error ("%s: trie has empty leaf", __func__);

      if (expanded && !prefix.empty ())
        return error ("%s: expanded trie has prefix", __func__);
      if (!expanded && IsPureEdge ())
        return error ("%s: non-expanded trie has pure edge", __func__);
    }

  BOOST_FOREACH(const PAIRTYPE(unsigned char, CUnoTrie*)& child, children)
    if (!child.second->Check (false, expanded))
      return false;

  return true;
}
