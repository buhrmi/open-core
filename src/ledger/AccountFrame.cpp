// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "AccountFrame.h"
#include "crypto/SecretKey.h"
#include "crypto/Hex.h"
#include "database/Database.h"
#include "LedgerDelta.h"
#include "ledger/LedgerManager.h"
#include "util/basen.h"
#include "util/Logging.h"
#include <algorithm>

using namespace soci;
using namespace std;

namespace stellar
{
using xdr::operator<;

const char* AccountFrame::kSQLCreateStatement1 =
    "CREATE TABLE accounts"
    "("
    "accountid       VARCHAR(56)  PRIMARY KEY,"
    "balance         BIGINT       NOT NULL CHECK (balance >= 0),"
    "seqnum          BIGINT       NOT NULL,"
    "numsubentries   INT          NOT NULL CHECK (numsubentries >= 0),"
    "inflationdest   VARCHAR(56),"
    "homedomain      VARCHAR(32),"
    "thresholds      TEXT,"
    "flags           INT          NOT NULL,"
    "lastmodified    INT          NOT NULL"
    ");";

const char* AccountFrame::kSQLCreateStatement2 =
    "CREATE TABLE signers"
    "("
    "accountid       VARCHAR(56) NOT NULL,"
    "publickey       VARCHAR(56) NOT NULL,"
    "weight          INT         NOT NULL,"
    "PRIMARY KEY (accountid, publickey)"
    ");";

const char* AccountFrame::kSQLCreateStatement3 =
    "CREATE INDEX signersaccount ON signers (accountid)";

const char* AccountFrame::kSQLCreateStatement4 = "CREATE INDEX accountbalances "
                                                 "ON Accounts (balance) WHERE "
                                                 "balance >= 1000000000";

AccountFrame::AccountFrame()
    : EntryFrame(ACCOUNT), mAccountEntry(mEntry.data.account())
{
    mAccountEntry.thresholds[0] = 1; // by default, master key's weight is 1
    mUpdateSigners = false;
    isnew = false;
}

AccountFrame::AccountFrame(LedgerEntry const& from)
    : EntryFrame(from), mAccountEntry(mEntry.data.account())
{
    mUpdateSigners = !mAccountEntry.signers.empty();
    isnew = false;
}

AccountFrame::AccountFrame(AccountFrame const& from) : AccountFrame(from.mEntry)
{
   isnew = false;
}

AccountFrame::AccountFrame(AccountID const& id) : AccountFrame()
{
    mAccountEntry.accountID = id;
    isnew = false;
}

AccountFrame::pointer
AccountFrame::makeAuthOnlyAccount(AccountID const& id)
{
    AccountFrame::pointer ret = make_shared<AccountFrame>(id);
    // puts a negative balance to trip any attempt to save this
    ret->mAccountEntry.balance = INT64_MIN;

    return ret;
}

void
AccountFrame::normalize()
{
    std::sort(mAccountEntry.signers.begin(), mAccountEntry.signers.end(),
              [](Signer const& s1, Signer const& s2)
              {
                  return s1.pubKey < s2.pubKey;
              });
}

bool
AccountFrame::isAuthRequired() const
{
    return (mAccountEntry.flags & AUTH_REQUIRED_FLAG);
}

int64_t
AccountFrame::getBalance() const
{
    return (mAccountEntry.balance);
}

int64_t
AccountFrame::getMinimumBalance(LedgerManager const& lm) const
{
    return lm.getMinBalance(mAccountEntry.numSubEntries);
}

int64_t
AccountFrame::getBalanceAboveReserve(LedgerManager const& lm) const
{
    int64_t avail =
        getBalance() - lm.getMinBalance(mAccountEntry.numSubEntries);
    if (avail < 0)
    {
        // nothing can leave this account if below the reserve
        // (this can happen if the reserve is raised)
        avail = 0;
    }
    return avail;
}

bool
AccountFrame::getIsNew()
{
    return isnew;
}

// returns true if successfully updated,
// false if balance is not sufficient
bool
AccountFrame::addNumEntries(int count, LedgerManager const& lm)
{
    int newEntriesCount = mAccountEntry.numSubEntries + count;
    if (newEntriesCount < 0)
    {
        throw std::runtime_error("invalid account state");
    }
    // only check minBalance when attempting to add subEntries
    if (count > 0 && getBalance() < lm.getMinBalance(newEntriesCount))
    {
        // balance too low
        return false;
    }
    mAccountEntry.numSubEntries = newEntriesCount;
    return true;
}

AccountID const&
AccountFrame::getID() const
{
    return (mAccountEntry.accountID);
}

uint32_t
AccountFrame::getMasterWeight() const
{
    return mAccountEntry.thresholds[THRESHOLD_MASTER_WEIGHT];
}

uint32_t
AccountFrame::getHighThreshold() const
{
    return mAccountEntry.thresholds[THRESHOLD_HIGH];
}

uint32_t
AccountFrame::getMediumThreshold() const
{
    return mAccountEntry.thresholds[THRESHOLD_MED];
}

void
AccountFrame::setIsNew()
{
    isnew = true;
}

uint32_t
AccountFrame::getLowThreshold() const
{
    return mAccountEntry.thresholds[THRESHOLD_LOW];
}

AccountFrame::pointer
AccountFrame::loadAccount(AccountID const& accountID, Database& db)
{
    LedgerKey key;
    uint32_t sqlIsNew;
    key.type(ACCOUNT);
    key.account().accountID = accountID;
    if (cachedEntryExists(key, db))
    {
        auto p = getCachedEntry(key, db);
        return p ? std::make_shared<AccountFrame>(*p) : nullptr;
    }

    std::string actIDStrKey = PubKeyUtils::toStrKey(accountID);

    std::string publicKey, inflationDest, creditAuthKey;
    std::string homeDomain, thresholds;
    soci::indicator inflationDestInd, homeDomainInd, thresholdsInd;

    AccountFrame::pointer res = make_shared<AccountFrame>(accountID);
    AccountEntry& account = res->getAccount();
    CLOG(INFO, "Database") << "BEFORE getisnew is: " << res->getIsNew();
    auto prep =
        db.getPreparedStatement("SELECT "
"balance, seqnum, numsubentries, inflationdest, homedomain, thresholds, flags,lastmodified, 0 as isnew "
"FROM accounts WHERE accountid=:v1 "
"UNION SELECT "
"0 as balance, 0 as seqnum, 0 as numsubentries, null as inflationdest, NULL as homedomain,'AQAAAA==' as thresholds,0 as flags,0 as lastmodified, 1 as isnew  "
"WHERE NOT EXISTS (SELECT * FROM accounts WHERE accountid=:v1)");
    auto& st = prep.statement();
    st.exchange(into(account.balance));
    st.exchange(into(account.seqNum));
    st.exchange(into(account.numSubEntries));
    st.exchange(into(inflationDest, inflationDestInd));
    st.exchange(into(homeDomain, homeDomainInd));
    st.exchange(into(thresholds, thresholdsInd));
    st.exchange(into(account.flags));
    st.exchange(into(res->getLastModified()));
    st.exchange(into(sqlIsNew));
    st.exchange(use(actIDStrKey, "v1"));
    st.define_and_bind();
    {
        auto timer = db.getSelectTimer("account");
        st.execute(true);
    }

    if (!st.got_data())
    {
        putCachedEntry(key, nullptr, db);
        return nullptr;
    }
    if (sqlIsNew == 1) {
        res->setIsNew();
    }
    CLOG(INFO, "Database") << actIDStrKey <<" sqlisnew is: " << sqlIsNew;
    CLOG(INFO, "Database") << actIDStrKey <<" isnew is: " << res->isnew;


    if (homeDomainInd == soci::i_ok)
    {
        account.homeDomain = homeDomain;
    }

    if (thresholdsInd == soci::i_ok)
    {
        bn::decode_b64(thresholds.begin(), thresholds.end(),
                       res->mAccountEntry.thresholds.begin());
    }

    if (inflationDestInd == soci::i_ok)
    {
        account.inflationDest.activate() =
            PubKeyUtils::fromStrKey(inflationDest);
    }

    account.signers.clear();

    if (account.numSubEntries != 0)
    {
        string pubKey;
        Signer signer;

        auto prep2 = db.getPreparedStatement("SELECT publickey, weight from "
                                             "signers where accountid =:id");
        auto& st2 = prep2.statement();
        st2.exchange(use(actIDStrKey));
        st2.exchange(into(pubKey));
        st2.exchange(into(signer.weight));
        st2.define_and_bind();
        {
            auto timer = db.getSelectTimer("signer");
            st2.execute(true);
        }
        while (st2.got_data())
        {
            signer.pubKey = PubKeyUtils::fromStrKey(pubKey);

            account.signers.push_back(signer);

            st2.fetch();
        }
    }

    res->normalize();
    res->mUpdateSigners = false;

    res->mKeyCalculated = false;
    res->putCachedEntry(db);
    return res;
}

bool
AccountFrame::exists(Database& db, LedgerKey const& key)
{
    if (cachedEntryExists(key, db) && getCachedEntry(key, db) != nullptr)
    {
        return true;
    }

    std::string actIDStrKey = PubKeyUtils::toStrKey(key.account().accountID);
    int exists = 0;
    {
        auto timer = db.getSelectTimer("account-exists");
        auto prep =
            db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM accounts "
                                    "WHERE accountid=:v1)");
        auto& st = prep.statement();
        st.exchange(use(actIDStrKey));
        st.exchange(into(exists));
        st.define_and_bind();
        st.execute(true);
    }
    return exists != 0;
}

uint64_t
AccountFrame::countObjects(soci::session& sess)
{
    uint64_t count = 0;
    sess << "SELECT COUNT(*) FROM accounts;", into(count);
    return count;
}

void
AccountFrame::storeDelete(LedgerDelta& delta, Database& db) const
{
    storeDelete(delta, db, getKey());
}

void
AccountFrame::storeDelete(LedgerDelta& delta, Database& db,
                          LedgerKey const& key)
{
    flushCachedEntry(key, db);

    std::string actIDStrKey = PubKeyUtils::toStrKey(key.account().accountID);
    {
        auto timer = db.getDeleteTimer("account");
        auto prep = db.getPreparedStatement(
            "DELETE from accounts where accountid= :v1");
        auto& st = prep.statement();
        st.exchange(soci::use(actIDStrKey));
        st.define_and_bind();
        st.execute(true);
    }
    {
        auto timer = db.getDeleteTimer("signer");
        auto prep =
            db.getPreparedStatement("DELETE from signers where accountid= :v1");
        auto& st = prep.statement();
        st.exchange(soci::use(actIDStrKey));
        st.define_and_bind();
        st.execute(true);
    }
    delta.deleteEntry(key);
}

void
AccountFrame::storeUpdate(LedgerDelta& delta, Database& db, bool insert)
{
    touch(delta);

    flushCachedEntry(db);

    std::string actIDStrKey = PubKeyUtils::toStrKey(mAccountEntry.accountID);
    std::string sql;
    CLOG(INFO, "Database") << "::" << actIDStrKey << "::";
    CLOG(INFO, "Database") << "getIsNew(): " << getIsNew();

    if (getIsNew())
    {
       
        sql = std::string(
            "INSERT INTO accounts ( accountid, balance, seqnum, "
            "numsubentries, inflationdest, homedomain, thresholds, flags, "
            "lastmodified ) "
            "VALUES ( :id, :v1, :v2, :v3, :v4, :v5, :v6, :v7, :v8 )");
    }
    else
    {
        CLOG(INFO, "Database") << "Updating account because: " << getIsNew();
        sql = std::string(
            "UPDATE accounts SET balance = :v1, seqnum = :v2, "
            "numsubentries = :v3, "
            "inflationdest = :v4, homedomain = :v5, thresholds = :v6, "
            "flags = :v7, lastmodified = :v8 WHERE accountid = :id");
    }
    CLOG(INFO, "Database") << "setting to 0 for " << actIDStrKey;
    isnew = false;
    
    auto prep = db.getPreparedStatement(sql);

    soci::indicator inflation_ind = soci::i_null;
    string inflationDestStrKey;

    if (mAccountEntry.inflationDest)
    {
        inflationDestStrKey =
            PubKeyUtils::toStrKey(*mAccountEntry.inflationDest);
        inflation_ind = soci::i_ok;
    }

    string thresholds(bn::encode_b64(mAccountEntry.thresholds));

    {
        soci::statement& st = prep.statement();
        st.exchange(use(actIDStrKey, "id"));
        st.exchange(use(mAccountEntry.balance, "v1"));
        st.exchange(use(mAccountEntry.seqNum, "v2"));
        st.exchange(use(mAccountEntry.numSubEntries, "v3"));
        st.exchange(use(inflationDestStrKey, inflation_ind, "v4"));
        st.exchange(use(string(mAccountEntry.homeDomain), "v5"));
        st.exchange(use(thresholds, "v6"));
        st.exchange(use(mAccountEntry.flags, "v7"));
        st.exchange(use(getLastModified(), "v8"));
        st.define_and_bind();
        {
            auto timer = insert ? db.getInsertTimer("account")
                                : db.getUpdateTimer("account");
            st.execute(true);
        }

        if (st.get_affected_rows() != 1)
        {
            throw std::runtime_error("Could not update data in SQL (account)");
        }
        if (insert)
        {
            delta.addEntry(*this);
        }
        else
        {
            delta.modEntry(*this);
        }
    }

    if (mUpdateSigners)
    {
        // instead separate signatures from account, just like offers are
        // separate entities
        AccountFrame::pointer startAccountFrame;
        startAccountFrame = loadAccount(getID(), db);
        if (!startAccountFrame)
        {
            throw runtime_error("could not load account!");
        }
        AccountEntry& startAccount = startAccountFrame->mAccountEntry;

        // deal with changes to Signers
        if (mAccountEntry.signers.size() < startAccount.signers.size())
        { // some signers were removed
            for (auto const& startSigner : startAccount.signers)
            {
                bool found = false;
                for (auto const& finalSigner : mAccountEntry.signers)
                {
                    if (finalSigner.pubKey == startSigner.pubKey)
                    {
                        if (finalSigner.weight != startSigner.weight)
                        {
                            std::string signerStrKey =
                                PubKeyUtils::toStrKey(finalSigner.pubKey);
                            {
                                auto timer = db.getUpdateTimer("signer");
                                auto prep2 = db.getPreparedStatement(
                                    "UPDATE signers set weight=:v1 where "
                                    "accountid=:v2 and publickey=:v3");
                                auto& st = prep2.statement();
                                st.exchange(use(finalSigner.weight));
                                st.exchange(use(actIDStrKey));
                                st.exchange(use(signerStrKey));
                                st.define_and_bind();
                                st.execute(true);
                            }
                        }
                        found = true;
                        break;
                    }
                }
                if (!found)
                { // delete signer
                    std::string signerStrKey =
                        PubKeyUtils::toStrKey(startSigner.pubKey);

                    auto prep2 =
                        db.getPreparedStatement("DELETE from signers where "
                                                "accountid=:v2 and "
                                                "publickey=:v3");
                    auto& st = prep2.statement();
                    st.exchange(use(actIDStrKey));
                    st.exchange(use(signerStrKey));
                    st.define_and_bind();
                    {
                        auto timer = db.getDeleteTimer("signer");
                        st.execute(true);
                    }

                    if (st.get_affected_rows() != 1)
                    {
                        throw std::runtime_error(
                            "Could not update data in SQL (signer)");
                    }
                }
            }
        }
        else
        { // signers added or the same
            for (auto const& finalSigner : mAccountEntry.signers)
            {
                bool found = false;
                for (auto const& startSigner : startAccount.signers)
                {
                    if (finalSigner.pubKey == startSigner.pubKey)
                    {
                        if (finalSigner.weight != startSigner.weight)
                        {
                            std::string signerStrKey =
                                PubKeyUtils::toStrKey(finalSigner.pubKey);
                            auto prep2 = db.getPreparedStatement(
                                "UPDATE signers set weight=:v1 where "
                                "accountid=:v2 and publickey=:v3");
                            auto& st = prep2.statement();
                            st.exchange(use(finalSigner.weight));
                            st.exchange(use(actIDStrKey));
                            st.exchange(use(signerStrKey));
                            st.define_and_bind();
                            st.execute(true);

                            if (st.get_affected_rows() != 1)
                            {
                                throw std::runtime_error(
                                    "Could not update data in SQL (signer2)");
                            }
                        }
                        found = true;
                        break;
                    }
                }
                if (!found)
                { // new signer
                    std::string signerStrKey =
                        PubKeyUtils::toStrKey(finalSigner.pubKey);

                    auto prep2 =
                        db.getPreparedStatement("INSERT INTO signers "
                                                "(accountid,publickey,weight) "
                                                "VALUES (:v1,:v2,:v3)");
                    auto& st = prep2.statement();
                    st.exchange(use(actIDStrKey));
                    st.exchange(use(signerStrKey));
                    st.exchange(use(finalSigner.weight));
                    st.define_and_bind();
                    st.execute(true);

                    if (st.get_affected_rows() != 1)
                    {
                        throw std::runtime_error(
                            "Could not update data in SQL (new signer)");
                    }
                }
            }
        }

        // Flush again to ensure changed signers are reloaded.
        flushCachedEntry(db);
    }
}

void
AccountFrame::storeChange(LedgerDelta& delta, Database& db)
{
    storeUpdate(delta, db, false);
}

void
AccountFrame::storeAdd(LedgerDelta& delta, Database& db)
{
    storeUpdate(delta, db, true);
}

void
AccountFrame::processForInflation(
    std::function<bool(AccountFrame::InflationVotes const&)> inflationProcessor,
    int maxWinners, Database& db)
{
    soci::session& session = db.getSession();

    InflationVotes v;
    std::string inflationDest;

    soci::statement st =
        (session.prepare
             << "SELECT"
                " sum(balance) AS votes, inflationdest FROM accounts WHERE"
                " inflationdest IS NOT NULL"
                " AND balance >= 1000000000 GROUP BY inflationdest"
                " ORDER BY votes DESC, inflationdest DESC LIMIT :lim",
         into(v.mVotes), into(inflationDest), use(maxWinners));

    st.execute(true);

    while (st.got_data())
    {
        v.mInflationDest = PubKeyUtils::fromStrKey(inflationDest);
        if (!inflationProcessor(v))
        {
            break;
        }
        st.fetch();
    }
}

void
AccountFrame::dropAll(Database& db)
{
    db.getSession() << "DROP TABLE IF EXISTS accounts;";
    db.getSession() << "DROP TABLE IF EXISTS signers;";

    db.getSession() << kSQLCreateStatement1;
    db.getSession() << kSQLCreateStatement2;
    db.getSession() << kSQLCreateStatement3;
    db.getSession() << kSQLCreateStatement4;
}
}
