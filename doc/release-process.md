Release Process
====================

## Branch updates

### Before every release candidate

* ( **Not in Namecoin yet.** ) Update translations see [translation_process.md](https://github.com/namecoin/namecoin-core/blob/master/doc/translation_process.md#synchronising-translations).
* Update manpages, see [gen-manpages.sh](https://github.com/namecoin/namecoin-core/blob/master/contrib/devtools/README.md#gen-manpagessh).
* Update release candidate version in `configure.ac` (`CLIENT_VERSION_RC`).

### Before every major and minor release

* ( **Not in Namecoin yet.** ) Update [bips.md](bips.md) to account for changes since the last release (don't forget to bump the version number on the first line).
* Update version in `configure.ac` (don't forget to set `CLIENT_VERSION_RC` to `0`).
* Write release notes (see "Write the release notes" below).

### Before every major release

( **These are handled by upstream Bitcoin Core, not Namecoin.** )

* On both the master branch and the new release branch:
  - update `CLIENT_VERSION_MAJOR` in [`configure.ac`](../configure.ac)
  - update `CLIENT_VERSION_MAJOR`, `PACKAGE_VERSION`, and `PACKAGE_STRING` in [`build_msvc/bitcoin_config.h`](/build_msvc/bitcoin_config.h)
* On the new release branch in [`configure.ac`](../configure.ac) and [`build_msvc/bitcoin_config.h`](/build_msvc/bitcoin_config.h) (see [this commit](https://github.com/bitcoin/bitcoin/commit/742f7dd)):
  - set `CLIENT_VERSION_MINOR` to `0`
  - set `CLIENT_VERSION_BUILD` to `0`
  - set `CLIENT_VERSION_IS_RELEASE` to `true`

#### Before branch-off

* Update hardcoded [seeds](/contrib/seeds/README.md), see [this pull request](https://github.com/bitcoin/bitcoin/pull/7415) for an example.
* Update [`src/chainparams.cpp`](/src/chainparams.cpp) m_assumed_blockchain_size and m_assumed_chain_state_size with the current size plus some overhead (see [this](#how-to-calculate-assumed-blockchain-and-chain-state-size) for information on how to calculate them).
* Update [`src/chainparams.cpp`](/src/chainparams.cpp) chainTxData with statistics about the transaction count and rate. Use the output of the `getchaintxstats` RPC, see
  [this pull request](https://github.com/bitcoin/bitcoin/pull/20263) for an example. Reviewers can verify the results by running `getchaintxstats <window_block_count> <window_final_block_hash>` with the `window_block_count` and `window_final_block_hash` from your output.
* Update `src/chainparams.cpp` nMinimumChainWork and defaultAssumeValid (and the block height comment) with information from the `getblockheader` (and `getblockhash`) RPCs.
  - The selected value must not be orphaned so it may be useful to set the value two blocks back from the tip.
  - Testnet should be set some tens of thousands back from the tip due to reorgs there.
  - This update should be reviewed with a reindex-chainstate with assumevalid=0 to catch any defect
     that causes rejection of blocks in the past history.
- Clear the release notes and move them to the wiki (see "Write the release notes" below).
- ( **Not in Namecoin yet.** ) Translations on Transifex
    - Create [a new resource](https://www.transifex.com/bitcoin/bitcoin/content/) named after the major version with the slug `[bitcoin.qt-translation-<RRR>x]`, where `RRR` is the major branch number padded with zeros. Use `src/qt/locale/bitcoin_en.xlf` to create it.
    - In the project workflow settings, ensure that [Translation Memory Fill-up](https://docs.transifex.com/translation-memory/enabling-autofill) is enabled and that [Translation Memory Context Matching](https://docs.transifex.com/translation-memory/translation-memory-with-context) is disabled.
    - Update the Transifex slug in [`.tx/config`](/.tx/config) to the slug of the resource created in the first step. This identifies which resource the translations will be synchronized from.
    - Make an announcement that translators can start translating for the new version. You can use one of the [previous announcements](https://www.transifex.com/bitcoin/bitcoin/announcements/) as a template.
    - Change the auto-update URL for the resource to `master`, e.g. `https://raw.githubusercontent.com/bitcoin/bitcoin/master/src/qt/locale/bitcoin_en.xlf`. (Do this only after the previous steps, to prevent an auto-update from interfering.)

#### After branch-off (on the major release branch)

- Update the versions.
- Create a pinned meta-issue for testing the release candidate (see [this issue](https://github.com/bitcoin/bitcoin/issues/17079) for an example) and provide a link to it in the release announcements where useful.
- ( **Not in Namecoin yet.** ) Translations on Transifex
    - Change the auto-update URL for the new major version's resource away from `master` and to the branch, e.g. `https://raw.githubusercontent.com/bitcoin/bitcoin/<branch>/src/qt/locale/bitcoin_en.xlf`. Do not forget this or it will keep tracking the translations on master instead, drifting away from the specific major release.

#### Before final release

- Merge the release notes from the wiki into the branch.
- Ensure the "Needs release note" label is removed from all relevant pull requests and issues.

#### Tagging a release (candidate)

To tag the version (or release candidate) in git, use the `make-tag.py` script from [bitcoin-maintainer-tools](https://github.com/bitcoin-core/bitcoin-maintainer-tools). From the root of the repository run:

    ../bitcoin-maintainer-tools/make-tag.py nc(new version, e.g. 0.20.0)

This will perform a few last-minute consistency checks in the build system files, and if they pass, create a signed tag.

## Building

### First time / New builders

Install Guix using one of the installation methods detailed in
[contrib/guix/INSTALL.md](/contrib/guix/INSTALL.md).

Check out the source code in the following directory hierarchy.

    cd /path/to/your/toplevel/build
    git clone https://github.com/namecoin/guix.sigs.git
    #git clone https://github.com/namecoin/namecoin-detached-sigs.git # Namecoin doesn't use detached sigs yet, so don't do this.
    git clone https://github.com/namecoin/namecoin-core.git

### Write the release notes

( **Not in Namecoin yet.** )

Open a draft of the release notes for collaborative editing at https://github.com/bitcoin-core/bitcoin-devwiki/wiki.

For the period during which the notes are being edited on the wiki, the version on the branch should be wiped and replaced with a link to the wiki which should be used for all announcements until `-final`.

Generate the change log. As this is a huge amount of work to do manually, there is the `list-pulls` script to do a pre-sorting step based on github PR metadata. See the [documentation in the README.md](https://github.com/bitcoin-core/bitcoin-maintainer-tools/blob/master/README.md#list-pulls).

Generate list of authors:

    git log --format='- %aN' nc(current version, e.g. 0.20.0)..nc(new version, e.g. 0.20.1) | sort -fiu

### Setup and perform Guix builds

Checkout the Namecoin Core version you'd like to build:

```sh
pushd ./namecoin-core
SIGNER='(your builder key, ie JeremyRand, jonasbits, etc)'
VERSION='(new version without nc-prefix, e.g. 0.20.0)'
git fetch "nc${VERSION}"
git checkout "nc${VERSION}"
popd
```

Ensure your guix.sigs are up-to-date if you wish to `guix-verify` your builds
against other `guix-attest` signatures.

```sh
git -C ./guix.sigs pull
```

### Create the macOS SDK tarball: (first time, or when SDK version changes)

Create the macOS SDK tarball, see the [macdeploy
instructions](/contrib/macdeploy/README.md#deterministic-macos-dmg-notes) for
details.

### Build and attest to build outputs:

Follow the relevant Guix README.md sections:
- [Performing a build](/contrib/guix/README.md#performing-a-build)
- [Attesting to build outputs](/contrib/guix/README.md#attesting-to-build-outputs)

### Verify other builders' signatures to your own. (Optional)

Add other builders keys to your gpg keyring, and/or refresh keys: See `../namecoin-core/contrib/builder-keys/README.md`.

Follow the relevant Guix README.md sections:
- [Verifying build output attestations](/contrib/guix/README.md#verifying-build-output-attestations)

### Next steps:

Commit your signature to guix.sigs:

```sh
pushd ./guix.sigs
git add "${VERSION}/${SIGNER}"/noncodesigned.SHA256SUMS{,.asc}
git commit -m "Add ${VERSION} unsigned sigs for ${SIGNER}"
git push  # Assuming you can push to the guix.sigs tree
popd
```

( **Not in Namecoin yet.** )

Codesigner only: Create Windows/macOS detached signatures:
- Only one person handles codesigning. Everyone else should skip to the next step.
- Only once the Windows/macOS builds each have 3 matching signatures may they be signed with their respective release keys.

Codesigner only: Sign the macOS binary:

    transfer bitcoin-osx-unsigned.tar.gz to macOS for signing
    tar xf bitcoin-osx-unsigned.tar.gz
    ./detached-sig-create.sh -s "Key ID"
    Enter the keychain password and authorize the signature
    Move signature-osx.tar.gz back to the guix-build host

Codesigner only: Sign the windows binaries:

    tar xf bitcoin-win-unsigned.tar.gz
    ./detached-sig-create.sh -key /path/to/codesign.key
    Enter the passphrase for the key when prompted
    signature-win.tar.gz will be created

Code-signer only: It is advised to test that the code signature attaches properly prior to tagging by performing the `guix-codesign` step.
However if this is done, once the release has been tagged in the bitcoin-detached-sigs repo, the `guix-codesign` step must be performed again in order for the guix attestation to be valid when compared against the attestations of non-codesigner builds.

Codesigner only: Commit the detached codesign payloads:

```sh
pushd ./namecoin-detached-sigs
# checkout the appropriate branch for this release series
rm -rf ./*
tar xf signature-osx.tar.gz
tar xf signature-win.tar.gz
git add -A
git commit -m "point to ${VERSION}"
git tag -s "nc${VERSION}" HEAD
git push the current branch and new tag
popd
```

Non-codesigners: wait for Windows/macOS detached signatures:

- Once the Windows/macOS builds each have 3 matching signatures, they will be signed with their respective release keys.
- Detached signatures will then be committed to the [namecoin-detached-sigs](https://github.com/namecoin/namecoin-detached-sigs) repository, which can be combined with the unsigned apps to create signed binaries.

( **Not in Namecoin yet.** ) Create (and optionally verify) the codesigned outputs:

- [Codesigning](/contrib/guix/README.md#codesigning)

( **Not in Namecoin yet.** ) Commit your signature for the signed macOS/Windows binaries:

```sh
pushd ./guix.sigs
git add "${VERSION}/${SIGNER}"/all.SHA256SUMS{,.asc}
git commit -m "Add attestations by ${SIGNER} for ${VERSION} codesigned"
git push  # Assuming you can push to the guix.sigs tree
popd
```

### After 3 or more people have guix-built and their results match:

Combine the `all.SHA256SUMS.asc` file from all signers into `SHA256SUMS.asc`:

```bash
cat "$VERSION"/*/all.SHA256SUMS.asc > SHA256SUMS.asc
```


- Upload to the namecoin.org server (`/var/www/bin/namecoin-core-${VERSION}/`):
    1. The contents of each `./namecoin-core/guix-build-${VERSION}/output/${HOST}/` directory, except for
       `*-debug*` files.

       Guix will output all of the results into host subdirectories, but the SHA256SUMS
       file does not include these subdirectories. In order for downloads via torrent
       to verify without directory structure modification, all of the uploaded files
       need to be in the same directory as the SHA256SUMS file.

       The `*-debug*` files generated by the guix build contain debug symbols
       for troubleshooting by developers. It is assumed that anyone that is
       interested in debugging can run guix to generate the files for
       themselves. To avoid end-user confusion about which file to pick, as well
       as save storage space *do not upload these to the namecoin.org server,
       nor put them in the torrent*.

       ```sh
       find guix-build-${VERSION}/output/ -maxdepth 2 -type f -not -name "SHA256SUMS.part" -and -not -name "*debug*" -exec scp {} user@namecoin.org:/var/www/bin/namecoin-core-${VERSION} \;
       ```

    2. The `SHA256SUMS` file

    3. The `SHA256SUMS.asc` combined signature file you just created

( **The following is not in Namecoin yet.** )

- Create a torrent of the `/var/www/bin/namecoin-core-${VERSION}` directory such
  that at the top level there is only one file: the `namecoin-core-${VERSION}`
  directory containing everything else. Name the torrent
  `namecoin-${VERSION}.torrent` (note that there is no `-core-` in this name).

  Optionally help seed this torrent. To get the `magnet:` URI use:

  ```sh
  transmission-show -m <torrent file>
  ```

  Insert the magnet URI into the announcement sent to mailing lists. This permits
  people without access to `namecoin.org` to download the binary distribution.
  Also put it into the `optional_magnetlink:` slot in the YAML file for
  namecoin.org.

- Update other repositories and websites for new version

  - namecoin.org blog post

  - bitcoincore.org maintained versions update:
    [table](https://github.com/bitcoin-core/bitcoincore.org/commits/master/_includes/posts/maintenance-table.md)

  - bitcoincore.org RPC documentation update

      - Install [golang](https://golang.org/doc/install)

      - Install the new Bitcoin Core release

      - Run bitcoind on regtest

      - Clone the [bitcoincore.org repository](https://github.com/bitcoin-core/bitcoincore.org)

      - Run: `go run generate.go` while being in `contrib/doc-gen` folder, and with bitcoin-cli in PATH

      - Add the generated files to git

  - Update packaging repo

      - Push the flatpak to flathub, e.g. https://github.com/flathub/org.bitcoincore.bitcoin-qt/pull/2

      - Push the latest version to master (if applicable), e.g. https://github.com/bitcoin-core/packaging/pull/32

      - Create a new branch for the major release "0.xx" from master (used to build the snap package) and request the
        track (if applicable), e.g. https://forum.snapcraft.io/t/track-request-for-bitcoin-core-snap/10112/7

      - Notify MarcoFalke so that he can start building the snap package

        - https://code.launchpad.net/~bitcoin-core/bitcoin-core-snap/+git/packaging (Click "Import Now" to fetch the branch)
        - https://code.launchpad.net/~bitcoin-core/bitcoin-core-snap/+git/packaging/+ref/0.xx (Click "Create snap package")
        - Name it "bitcoin-core-snap-0.xx"
        - Leave owner and series as-is
        - Select architectures that are compiled via guix
        - Leave "automatically build when branch changes" unticked
        - Tick "automatically upload to store"
        - Put "bitcoin-core" in the registered store package name field
        - Tick the "edge" box
        - Put "0.xx" in the track field
        - Click "create snap package"
        - Click "Request builds" for every new release on this branch (after updating the snapcraft.yml in the branch to reflect the latest guix results)
        - Promote release on https://snapcraft.io/bitcoin-core/releases if it passes sanity checks

  - This repo

      - Archive the release notes for the new version to `doc/release-notes/` (branch `master` and branch of the release)

      - Create a [new GitHub release](https://github.com/namecoin/namecoin-core/releases/new) with a link to the archived release notes

- Announce the release:

  - bitcoin-dev and bitcoin-core-dev mailing list

  - Bitcoin Core announcements list https://bitcoincore.org/en/list/announcements/join/

  - Namecoin Twitter https://twitter.com/namecoin

  - Celebrate

### Additional information

#### <a name="how-to-calculate-assumed-blockchain-and-chain-state-size"></a>How to calculate `m_assumed_blockchain_size` and `m_assumed_chain_state_size`

Both variables are used as a guideline for how much space the user needs on their drive in total, not just strictly for the blockchain.
Note that all values should be taken from a **fully synced** node and have an overhead of 5-10% added on top of its base value.

To calculate `m_assumed_blockchain_size`:
- For `mainnet` -> Take the size of the data directory, excluding `/regtest` and `/testnet3` directories.
- For `testnet` -> Take the size of the `/testnet3` directory.


To calculate `m_assumed_chain_state_size`:
- For `mainnet` -> Take the size of the `/chainstate` directory.
- For `testnet` -> Take the size of the `/testnet3/chainstate` directory.

Notes:
- When taking the size for `m_assumed_blockchain_size`, there's no need to exclude the `/chainstate` directory since it's a guideline value and an overhead will be added anyway.
- The expected overhead for growth may change over time, so it may not be the same value as last release; pay attention to that when changing the variables.
