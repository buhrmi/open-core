[![Build Status](https://travis-ci.org/stellar/stellar-core.svg?branch=auto)](https://travis-ci.org/stellar/stellar-core)

# stellar-core

The Core Consensus is basically the same as the [Stellar Consensus](http://www.stellar.org/galaxy) with two small differences with huge implications:

- The removal of native currency
- Network Passphrase is ignored

Implications:

- Transactions are FREE (the fee is always zero)
- Addresses don't need to receive a "createAccount" Transaction before they can be used.
- Accounts don't need to be funded (no friendbot).
- There is no central authority in charge of giving away native currency.
- The genesis ledger is 100% EMPTY.
- Consensus does not care about assigned passphrase/networkID
- *TRULY* open network

# Getting Started

## Warning: Requires Postgres 9.5

You WILL lose sync if you run against an earlier version. YOU HAVE BEEN WARNED.

Installation instructions ---> [HERE](https://github.com/buhrmi/core/blob/master/INSTALL.md)

After installation, use [this configuration file](https://github.com/buhrmi/core/blob/master/docs/open-core.cfg) to configure your node to connect to the network.
 
# Running tests against postgreSQL

There are two options.  The easiest is to have the test suite just
create a temporary postgreSQL database cluster in /tmp and delete it
after the test.  That will happen by default if you run `make check`.

You can also create a temporary database cluster manually, by running
`./src/test/selftest-pg bash` to get a shell, then running tests
manually.  The advantage of this is that you can examine the database
log in `$PGDATA/pg_log/` after running tests, as well as manually
inspect the database with `psql`.

# Contributing

Just do your best! If you have any questions, join us in the developer chat at [Slack](https://stellar-public.slack.com/messages/dev/) and ask away.

# License

Yes.
