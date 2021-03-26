[![Build Status](https://travis-ci.org/open-core/network.svg?branch=master)](https://travis-ci.org/open-core/network)

> NOTE: This project never got any traction and my validator node is offline.

# open-core/network

Open Core is basically the same as [Stellar](http://www.stellar.org) but it

- does not have a native currency
- does not have a central authority
- does not have inflation
- does not have transaction fees
- does not need a "createAccount" transaction
- does not have a root account
- starts with a blank genesis ledger
- is truly open

# Getting Started

Installation instructions ---> [HERE](https://github.com/buhrmi/core/blob/master/INSTALL.md)

After installation, use [this configuration file](https://github.com/buhrmi/core/blob/master/docs/open-core.cfg) to configure your node to connect to the network. Note that this config file pre-configures my personal validator node (validator.open-core.org) in the quorum. DO NOT USE IT if you don't trust this node.

# Running tests against postgreSQL

There are two options.  The easiest is to have the test suite just
create a temporary postgreSQL database cluster in /tmp and delete it
after the test.  That will happen by default if you run `make check`.

You can also create a temporary database cluster manually, by running
`./src/test/selftest-pg bash` to get a shell, then running tests
manually.  The advantage of this is that you can examine the database
log in `$PGDATA/pg_log/` after running tests, as well as manually
inspect the database with `psql`.

