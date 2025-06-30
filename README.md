Namecoin Core integration/staging tree
=====================================

[![Build Status](https://travis-ci.org/namecoin/namecoin-core.svg?branch=master)](https://travis-ci.org/namecoin/namecoin-core)

https://namecoin.org

What is Namecoin? 
----------------

Namecoin is a decentralized open source information registration and transfer system based on the Bitcoin cryptocurrency.

What does it do?
----------------

* Securely record and transfer arbitrary names (keys).
* Attach a value (data) to the names (up to 520 bytes, more in the future).
* Transact namecoins, the digital currency (ℕ, NMC).

Namecoin was the first fork of Bitcoin and still is one of the most innovative altcoins. It was first to implement merged mining and a decentralized DNS. Namecoin squares Zooko's Triangle!

What can it be used for?
----------------

* Protect free-speech rights online by making the web more resistant to censorship.
* Access websites using the .bit domain (with TLS/SSL).
* Store identity information such as email, GPG key, BTC address, TLS fingerprints, Bitmessage address, etc.
* Human readable Tor .onion names/domains.
* File signatures, Voting, bonds/stocks,/shares, web of trust, escrow and notary services (to be implemented).

For more information, as well as an immediately useable, binary version of
the Namecoin Core software, see https://www.namecoin.org/download.

License
-------

Namecoin Core is released under the terms of the MIT license. See [COPYING](COPYING) for more
information or see https://opensource.org/licenses/MIT.

Development Process
-------------------

The `master` branch is regularly built (see doc/build-*.md for instructions) and tested, but is not guaranteed to be
completely stable. [Tags](https://github.com/namecoin/namecoin-core/tags) are created
regularly to indicate new official, stable release versions of Namecoin Core.

The contribution workflow is described in [CONTRIBUTING.md](CONTRIBUTING.md)
and useful hints for developers can be found in [doc/developer-notes.md](doc/developer-notes.md).

The developer [forum](https://forum.namecoin.info/viewforum.php?f=4)
should be used to discuss complicated or controversial changes before working
on a patch set.

Developer IRC can be found on Freenode at #namecoin-dev.

Testing
-------

Testing and code review is the bottleneck for development; we get more pull
requests than we can review and test on short notice. Please be patient and help out by testing
other people's pull requests, and remember this is a security-critical project where any mistake might cost people
lots of money.

### Automated Testing

Developers are strongly encouraged to write [unit tests](src/test/README.md) for new code, and to
submit new unit tests for old code. Unit tests can be compiled and run
(assuming they weren't disabled during the generation of the build system) with: `ctest`. Further details on running
and extending unit tests can be found in [/src/test/README.md](/src/test/README.md).

There are also [regression and integration tests](/test), written
in Python.
These tests can be run (if the [test dependencies](/test) are installed) with: `build/test/functional/test_runner.py`
(assuming `build` is your build directory).

The CI (Continuous Integration) systems make sure that every pull request is built for Windows, Linux, and macOS,
and that unit/sanity tests are run automatically.

### Manual Quality Assurance (QA) Testing

Changes should be tested by somebody other than the developer who wrote the
code. This is especially important for large or high-risk changes. It is useful
to add a test plan to the pull request description if testing the changes is
not straightforward.

Translations
------------

**Translation workflow is not yet set up for Namecoin Core.  For strings which are common to Bitcoin Core, see below.**

Changes to translations as well as new translations can be submitted to
[Bitcoin Core's Transifex page](https://explore.transifex.com/bitcoin/bitcoin/).

Translations are periodically pulled from Transifex and merged into the git repository. See the
[translation process](doc/translation_process.md) for details on how this works.

**Important**: We do not accept translation changes as GitHub pull requests because the next
pull from Transifex would automatically overwrite them again.
