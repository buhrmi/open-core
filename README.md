[![Build Status](https://travis-ci.org/buhrmi/aya.svg?branch=master)](https://travis-ci.org/buhrmi/aya)

# Aya

Aya is a replicated state machine using the the [Stellar Consensus Protocol](https://github.com/stellar/stellar-core/blob/master/src/scp/readme.md). It's mostly compatible to Stellar, but with some differences that I believe will put Aya in a position that allows it to become the easiest and most accessible blockchain in the world. These differences include:

- No `createAccount` transactions. It shouldn't be neccessary to enforce a `createAccount` transaction before a user can receive funds. All addresses should automatically be valid accounts.
- No trustlines. You can still send custom assets, but the receiver does not need to create a trustline first. every user automatically has an "unlimited" trustline for every asset.

I believe that with these small changes, the network will become much easier to use, and - given some time - might even be able to overtake the original Stellar network in terms of accessibility and consequently trade volume.

## About Stellar

Stellar is a replicated state machine that maintains a local copy of a cryptographic ledger and processes transactions against it, in consensus with a set of peers.
It implements the [Stellar Consensus Protocol](https://github.com/stellar/stellar-core/blob/master/src/scp/readme.md), a _federated_ consensus protocol.
It is written in C++14 and runs on Linux, OSX and Windows.
Learn more by reading the [overview document](https://github.com/stellar/stellar-core/blob/master/docs/readme.md).


# Documentation

> Note that the documentation is forked from Stellar. Be sure to replace the stellar repositories with the aya repositories, when following the instructions below. 

Documentation of the code's layout and abstractions, as well as for the
functionality available, can be found in
[`./docs`](https://github.com/stellar/stellar-core/tree/master/docs).

# Installation

See [Installation](./INSTALL.md)

# Contributing

See [Contributing](./CONTRIBUTING.md)

# Running tests

See [running tests](./CONTRIBUTING.md#running-tests)
