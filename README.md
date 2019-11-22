[![Build Status](https://travis-ci.org/buhrmi/open-core.svg?branch=auto)](https://travis-ci.org/buhrmi/open-core)

# opencore

Opencore is a fork of stellar with a lot of stuff that nobody likes going to be removed. That list includes:

- `createAccount` transactions. It shouldn't be neccessary to enforce a `createAccount` transaction before a user can receive funds. All addresses should automatically be valid accounts.
- Trustlines. You can still send custom assets, but the receiver does not need to create a trustline first. every user automatically has an "unlimited" trustline to every asset.

## Stellar

Stellar is a replicated state machine that maintains a local copy of a cryptographic ledger and processes transactions against it, in consensus with a set of peers.
It implements the [Stellar Consensus Protocol](https://github.com/stellar/stellar-core/blob/master/src/scp/readme.md), a _federated_ consensus protocol.
It is written in C++14 and runs on Linux, OSX and Windows.
Learn more by reading the [overview document](https://github.com/stellar/stellar-core/blob/master/docs/readme.md).

## Main (planned) differences between opencore and stellar



# Documentation

Documentation of the code's layout and abstractions, as well as for the
functionality available, can be found in
[`./docs`](https://github.com/stellar/stellar-core/tree/master/docs).

# Installation

See [Installation](./INSTALL.md)

# Contributing

See [Contributing](./CONTRIBUTING.md)

# Running tests

See [running tests](./CONTRIBUTING.md#running-tests)
