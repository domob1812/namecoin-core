// Copyright (c) 2014 Daniel Kraft
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "names/common.h"

#include "names/unotrie.h"
#include "script/names.h"
#include "util.h"
#include "utiltime.h"

bool fNameHistory = false;

/* ************************************************************************** */
/* CNameData.  */

void
CNameData::fromScript (unsigned h, const COutPoint& out,
                       const CNameScript& script)
{
  assert (script.isAnyUpdate ());
  value = script.getOpValue ();
  nHeight = h;
  prevout = out;
  addr = script.getAddress ();
}

/* ************************************************************************** */
/* CNameIterator.  */

CNameIterator::~CNameIterator ()
{
  /* Nothing to be done here.  This may be overwritten by
     subclasses if they need a destructor.  */
}

/* ************************************************************************** */
/* CNameCacheNameIterator.  */

class CCacheNameIterator : public CNameIterator
{

private:

  /** Reference to cache object that is used.  */
  const CNameCache& cache;

  /** Base iterator to combine with the cache.  */
  CNameIterator* base;

  /** Whether or not the base iterator has more entries.  */
  bool baseHasMore;
  /** "Next" name of the base iterator.  */
  valtype baseName;
  /** "Next" data of the base iterator.  */
  CNameData baseData;

  /** Iterator of the cache's entries.  */
  CNameCache::EntryMap::const_iterator cacheIter;

  /* Call the base iterator's next() routine to fill in the internal
     "cache" for the next entry.  This already skips entries that are
     marked as deleted in the cache.  */
  void advanceBaseIterator ();

public:

  /**
   * Construct the iterator.  This takes ownership of the base iterator.
   * @param c The cache object to use.
   * @param b The base iterator.
   */
  CCacheNameIterator (const CNameCache& c, CNameIterator* b);

  /* Destruct, this deletes also the base iterator.  */
  ~CCacheNameIterator ();

  /* Implement iterator methods.  */
  void seek (const valtype& name);
  bool next (valtype& name, CNameData& data);

};

CCacheNameIterator::CCacheNameIterator (const CNameCache& c, CNameIterator* b)
  : cache(c), base(b)
{
  /* Add a seek-to-start to ensure that everything is consistent.  This call
     may be superfluous if we seek to another position afterwards anyway,
     but it should also not hurt too much.  */
  seek (valtype ());
}

CCacheNameIterator::~CCacheNameIterator ()
{
  delete base;
}

void
CCacheNameIterator::advanceBaseIterator ()
{
  assert (baseHasMore);
  do
    baseHasMore = base->next (baseName, baseData);
  while (baseHasMore && cache.isDeleted (baseName));
}

void
CCacheNameIterator::seek (const valtype& start)
{
  cacheIter = cache.entries.lower_bound (start);
  base->seek (start);

  baseHasMore = true;
  advanceBaseIterator ();
}

bool
CCacheNameIterator::next (valtype& name, CNameData& data)
{
  /* Exit early if no more data is available in either the cache
     nor the base iterator.  */
  if (!baseHasMore && cacheIter == cache.entries.end ())
    return false;

  /* Determine which source to use for the next.  */
  bool useBase;
  if (!baseHasMore)
    useBase = false;
  else if (cacheIter == cache.entries.end ())
    useBase = true;
  else
    {
      /* A special case is when both iterators are equal.  In this case,
         we want to use the cached version.  We also have to advance
         the base iterator.  */
      if (baseName == cacheIter->first)
        advanceBaseIterator ();

      /* Due to advancing the base iterator above, it may happen that
         no more base entries are present.  Handle this gracefully.  */
      if (!baseHasMore)
        useBase = false;
      else
        {
          assert (baseName != cacheIter->first);

          CNameCache::NameComparator cmp;
          useBase = cmp (baseName, cacheIter->first);
        }
    }

  /* Use the correct source now and advance it.  */
  if (useBase)
    {
      name = baseName;
      data = baseData;
      advanceBaseIterator ();
    }
  else
    {
      name = cacheIter->first;
      data = cacheIter->second;
      ++cacheIter;
    }

  return true;
}

/* ************************************************************************** */
/* CNameCache.  */

bool
CNameCache::get (const valtype& name, CNameData& data) const
{
  const EntryMap::const_iterator i = entries.find (name);
  if (i == entries.end ())
    return false;

  data = i->second;
  return true;
}

void
CNameCache::set (const valtype& name, const CNameData& data)
{
  const std::set<valtype>::iterator di = deleted.find (name);
  if (di != deleted.end ())
    deleted.erase (di);

  const EntryMap::iterator ei = entries.find (name);
  if (ei != entries.end ())
    ei->second = data;
  else
    entries.insert (std::make_pair (name, data));
}

void
CNameCache::remove (const valtype& name)
{
  const EntryMap::iterator ei = entries.find (name);
  if (ei != entries.end ())
    entries.erase (ei);

  deleted.insert (name);
}

CNameIterator*
CNameCache::iterateNames (CNameIterator* base) const
{
  return new CCacheNameIterator (*this, base);
}

bool
CNameCache::getHistory (const valtype& name, CNameHistory& res) const
{
  assert (fNameHistory);

  const std::map<valtype, CNameHistory>::const_iterator i = history.find (name);
  if (i == history.end ())
    return false;

  res = i->second;
  return true;
}

void
CNameCache::setHistory (const valtype& name, const CNameHistory& data)
{
  assert (fNameHistory);

  const std::map<valtype, CNameHistory>::iterator ei = history.find (name);
  if (ei != history.end ())
    ei->second = data;
  else
    history.insert (std::make_pair (name, data));
}

void
CNameCache::updateNamesForHeight (unsigned nHeight,
                                  std::set<valtype>& names) const
{
  /* Seek in the map of cached entries to the first one corresponding
     to our height.  */

  const ExpireEntry seekEntry(nHeight, valtype ());
  std::map<ExpireEntry, bool>::const_iterator it;

  for (it = expireIndex.lower_bound (seekEntry); it != expireIndex.end (); ++it)
    {
      const ExpireEntry& cur = it->first;
      assert (cur.nHeight >= nHeight);
      if (cur.nHeight > nHeight)
        break;

      if (it->second)
        names.insert (cur.name);
      else
        names.erase (cur.name);
    }
}

void
CNameCache::addExpireIndex (const valtype& name, unsigned height)
{
  const ExpireEntry entry(height, name);
  expireIndex[entry] = true;
}

void
CNameCache::removeExpireIndex (const valtype& name, unsigned height)
{
  const ExpireEntry entry(height, name);
  expireIndex[entry] = false;
}

void
CNameCache::applyTo (CNameCache& cache) const
{
  for (EntryMap::const_iterator i = entries.begin ();
       i != entries.end (); ++i)
    cache.set (i->first, i->second);

  for (std::set<valtype>::const_iterator i = deleted.begin ();
       i != deleted.end (); ++i)
    cache.remove (*i);

  for (std::map<valtype, CNameHistory>::const_iterator i = history.begin ();
       i != history.end (); ++i)
    cache.setHistory (i->first, i->second);

  for (std::map<ExpireEntry, bool>::const_iterator i = expireIndex.begin ();
       i != expireIndex.end (); ++i)
    cache.expireIndex[i->first] = i->second;
}

void
CNameCache::applyTo (CUnoTrie& trie, bool expanded) const
{
#ifdef BENCHMARK_UNO_TRIE
  static int64_t nTotalMicros = 0;
  const int64_t nStart = GetTimeMicros ();
#endif // BENCHMARK_UNO_TRIE

  for (std::map<valtype, CNameData>::const_iterator i = entries.begin ();
       i != entries.end (); ++i)
    trie.Set (i->first.begin (), i->first.end (), i->second, expanded);

  for (std::set<valtype>::const_iterator i = deleted.begin ();
       i != deleted.end (); ++i)
    trie.Delete (i->begin (), i->end (), expanded);

#ifdef BENCHMARK_UNO_TRIE
  /* Compute the root hash, since that is a step that a miner
     or full node has to do for every block.  This routine is called
     whenever pcoinsTip is updated from the temporary cache, which corresponds
     to the addition of a new block in the chain.  */
  trie.GetHash ();

  const int64_t nEnd = GetTimeMicros ();
  assert (nEnd >= nStart);
  nTotalMicros += nEnd - nStart;
  LogPrint ("bench", "UNO in-memory update: %.2fms, total %.3fs\n",
            (nEnd - nStart) * 1e-3, nTotalMicros * 1e-6);
#endif // BENCHMARK_UNO_TRIE
}
