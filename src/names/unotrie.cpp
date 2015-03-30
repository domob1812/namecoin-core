// Copyright (c) 2014-2015 Daniel Kraft
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "names/unotrie.h"

/* ************************************************************************** */
/* CUnoTrie.  */

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

CUnoTrie::~CUnoTrie ()
{
  Clear ();
}
