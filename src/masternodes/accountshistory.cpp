// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <key_io.h>
#include <masternodes/accounts.h>
#include <masternodes/accountshistory.h>
#include <masternodes/masternodes.h>
#include <masternodes/vaulthistory.h>

struct AccountHistoryKeyNew {
    uint32_t blockHeight;
    CScript owner;
    uint32_t txn;  // for order in block

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        if (ser_action.ForRead()) {
            READWRITE(WrapBigEndian(blockHeight));
            blockHeight = ~blockHeight;
        } else {
            uint32_t blockHeight_ = ~blockHeight;
            READWRITE(WrapBigEndian(blockHeight_));
        }

        READWRITE(owner);

        if (ser_action.ForRead()) {
            READWRITE(WrapBigEndian(txn));
            txn = ~txn;
        } else {
            uint32_t txn_ = ~txn;
            READWRITE(WrapBigEndian(txn_));
        }
    }
};

static AccountHistoryKeyNew Convert(const AccountHistoryKey &key) {
    return {key.blockHeight, key.owner, key.txn};
}

static AccountHistoryKey Convert(const AccountHistoryKeyNew &key) {
    return {key.owner, key.blockHeight, key.txn};
}

void CAccountsHistoryView::CreateMultiIndexIfNeeded() {
    AccountHistoryKeyNew anyNewKey{~0u, {}, ~0u};
    if (auto it = LowerBound<ByAccountHistoryKeyNew>(anyNewKey); it.Valid()) {
        return;
    }

    LogPrintf("Adding multi index in progress...\n");

    auto startTime = GetTimeMillis();

    AccountHistoryKey startKey{{}, ~0u, ~0u};
    auto it = LowerBound<ByAccountHistoryKey>(startKey);
    for (; it.Valid(); it.Next()) {
        WriteBy<ByAccountHistoryKeyNew>(Convert(it.Key()), '\0');
    }

    Flush();

    LogPrint(BCLog::BENCH, "    - Multi index took: %dms\n", GetTimeMillis() - startTime);
}

void CAccountsHistoryView::ForEachAccountHistory(
    std::function<bool(const AccountHistoryKey &, AccountHistoryValue)> callback,
    const CScript &owner,
    uint32_t height,
    uint32_t txn) {
    if (!owner.empty()) {
        ForEach<ByAccountHistoryKey, AccountHistoryKey, AccountHistoryValue>(callback, {owner, height, txn});
        return;
    }

    ForEach<ByAccountHistoryKeyNew, AccountHistoryKeyNew, char>(
        [&](const AccountHistoryKeyNew &newKey, char) {
            auto key   = Convert(newKey);
            auto value = ReadAccountHistory(key);
            assert(value);
            return callback(key, *value);
        },
        {height, owner, txn});
}

std::optional<AccountHistoryValue> CAccountsHistoryView::ReadAccountHistory(const AccountHistoryKey &key) const {
    return ReadBy<ByAccountHistoryKey, AccountHistoryValue>(key);
}

Res CAccountsHistoryView::WriteAccountHistory(const AccountHistoryKey &key, const AccountHistoryValue &value) {
    WriteBy<ByAccountHistoryKey>(key, value);
    WriteBy<ByAccountHistoryKeyNew>(Convert(key), '\0');
    return Res::Ok();
}

Res CAccountsHistoryView::EraseAccountHistory(const AccountHistoryKey &key) {
    EraseBy<ByAccountHistoryKey>(key);
    EraseBy<ByAccountHistoryKeyNew>(Convert(key));
    return Res::Ok();
}

Res CAccountsHistoryView::EraseAccountHistoryHeight(uint32_t height) {
    std::vector<AccountHistoryKey> keysToDelete;

    auto it = LowerBound<ByAccountHistoryKeyNew>(AccountHistoryKeyNew{height, {}, ~0u});
    for (; it.Valid() && it.Key().blockHeight == height; it.Next()) {
        keysToDelete.push_back(Convert(it.Key()));
    }

    for (const auto &key : keysToDelete) {
        EraseAccountHistory(key);
    }
    return Res::Ok();
}

CAccountHistoryStorage::CAccountHistoryStorage(const fs::path &dbName, std::size_t cacheSize, bool fMemory, bool fWipe)
    : CStorageView(new CStorageLevelDB(dbName, cacheSize, fMemory, fWipe)) {}

CBurnHistoryStorage::CBurnHistoryStorage(const fs::path &dbName, std::size_t cacheSize, bool fMemory, bool fWipe)
    : CStorageView(new CStorageLevelDB(dbName, cacheSize, fMemory, fWipe)) {}

CAccountsHistoryWriter::CAccountsHistoryWriter(CCustomCSView &storage,
                                               uint32_t height,
                                               uint32_t txn,
                                               const uint256 &txid,
                                               uint8_t type,
                                               CHistoryWriters *writers)
    : CStorageView(new CFlushableStorageKV(static_cast<CStorageKV &>(storage.GetStorage()))),
      height(height),
      txn(txn),
      txid(txid),
      type(type),
      writers(writers) {}

Res CAccountsHistoryWriter::AddBalance(const CScript &owner, CTokenAmount amount) {
    auto res = CCustomCSView::AddBalance(owner, amount);
    if (writers && amount.nValue != 0 && res.ok) {
        writers->AddBalance(owner, amount, vaultID);
    }

    return res;
}

Res CAccountsHistoryWriter::AddPhantomBalance(const CScript &owner, CTokenAmount amount, uint8_t phantomreason) {
    if (writers && amount.nValue != 0) {
        writers->AddBalance(owner, amount, vaultID);
    }
    return Res::Ok();
}

Res CAccountsHistoryWriter::SubBalance(const CScript &owner, CTokenAmount amount) {
    auto res = CCustomCSView::SubBalance(owner, amount);
    if (writers && res.ok && amount.nValue != 0) {
        writers->SubBalance(owner, amount, vaultID);
    }

    return res;
}

Res CAccountsHistoryWriter::SubPhantomBalance(const CScript &owner, CTokenAmount amount, uint8_t phantomreason) {
    if (writers && amount.nValue != 0) {
        writers->SubBalance(owner, amount, vaultID);
    }

    return Res::Ok();
}

bool CAccountsHistoryWriter::Flush() {
    if (writers) {
        writers->Flush(height, txid, txn, type, vaultID);
    }
    return CCustomCSView::Flush();
}

CAccountHistoryStorage *CAccountsHistoryWriter::GetAccountHistoryStore() {
    return writers ? writers->GetAccountHistoryStore() : nullptr;
};

CHistoryWriters::CHistoryWriters(CAccountHistoryStorage *historyView,
                                 CBurnHistoryStorage *burnView,
                                 CVaultHistoryStorage *vaultView)
    : historyView(historyView),
      burnView(burnView),
      vaultView(vaultView) {}

extern std::string ScriptToString(const CScript &script);

void CHistoryWriters::AddBalance(const CScript &owner, const CTokenAmount amount, const uint256 &vaultID) {
    if (historyView) {
        diffs[owner][amount.nTokenId] += amount.nValue;
    }
    if (burnView && owner == Params().GetConsensus().burnAddress) {
        burnDiffs[owner][amount.nTokenId] += amount.nValue;
    }
    if (vaultView && !vaultID.IsNull()) {
        vaultDiffs[vaultID][owner][amount.nTokenId] += amount.nValue;
    }
}

void CHistoryWriters::AddPhantomBalance(const CScript &owner, const CTokenAmount amount, const uint256 &vaultID, uint8_t phantomType) {
    if (historyView) {
        phantomdiffs[vaultID][owner][phantomType][amount.nTokenId] += amount.nValue;
    }
}

void CHistoryWriters::SubPhantomBalance(const CScript &owner, const CTokenAmount amount, const uint256 &vaultID, uint8_t phantomType) {
    if (historyView) {
                phantomdiffs[vaultID][owner][phantomType][amount.nTokenId] -= amount.nValue;

    }
}

void CHistoryWriters::AddFeeBurn(const CScript &owner, const CAmount amount) {
    if (burnView && amount != 0) {
        burnDiffs[owner][DCT_ID{0}] += amount;
    }
}

void CHistoryWriters::SubBalance(const CScript &owner, const CTokenAmount amount, const uint256 &vaultID) {
    if (historyView) {
        diffs[owner][amount.nTokenId] -= amount.nValue;
    }
    if (burnView && owner == Params().GetConsensus().burnAddress) {
        burnDiffs[owner][amount.nTokenId] -= amount.nValue;
    }
    if (vaultView && !vaultID.IsNull()) {
        vaultDiffs[vaultID][owner][amount.nTokenId] -= amount.nValue;
    }
}

void CHistoryWriters::Flush(const uint32_t height,
                            const uint256 &txid,
                            const uint32_t txn,
                            const uint8_t type,
                            const uint256 &vaultID) {
    if (historyView) {
        for (const auto &diff : diffs) {
            LogPrint(BCLog::ACCOUNTCHANGE,
                     "AccountChange: txid=%s addr=%s change=%s\n",
                     txid.GetHex(),
                     ScriptToString(diff.first),
                     (CBalances{diff.second}.ToString()));
            historyView->WriteAccountHistory({diff.first, height, txn}, {txid, type, vaultID, diff.second});
        }

        // typedef std::map<uint8_t, TAmounts> TPhantomAmounts;

        // now also write phantom diffs
        uint32_t offset = txn + 1000;

        for (const auto &diff : phantomdiffs) {
            uint256 pseudoVaultId = diff.first;
            for (const auto &addresses : diff.second) {
                CScript owner = addresses.first;
                // typedef std::map<DCT_ID, std::pair<CAmount, uint8_t>> TPhantomAmounts;
                for (const auto &coins : addresses.second) {
                    uint8_t pseudotype = coins.first;
                    TAmounts amounts = coins.second;

                    LogPrint(BCLog::ACCOUNTCHANGE,
                     "PseudoAccountChange: txid=%s addr=%s change=%s\n",
                     txid.GetHex(),
                     ScriptToString(owner),
                     (CBalances{amounts}.ToString()));
                    
                    ++offset;
                    historyView->WriteAccountHistory({owner, height, txn + offset}, {txid, pseudotype, pseudoVaultId, amounts});
                }
               
            }
        }
    }
    if (burnView) {
        for (const auto &diff : burnDiffs) {
            burnView->WriteAccountHistory({diff.first, height, txn}, {txid, type, uint256S("0"), diff.second});
        }
    }
    if (vaultView) {
        for (const auto &diff : vaultDiffs) {
            for (const auto &addresses : diff.second) {
                vaultView->WriteVaultHistory({height, diff.first, txn, addresses.first},
                                             {txid, type, addresses.second});
            }
        }
        if (!schemeID.empty()) {
            vaultView->WriteVaultScheme({vaultID, height}, {type, txid, schemeID, txn});
        }
        if (!globalLoanScheme.identifier.empty()) {
            vaultView->WriteGlobalScheme({height, txn, globalLoanScheme.schemeCreationTxid},
                                         {globalLoanScheme, type, txid});
        }
    }
}

std::unique_ptr<CAccountHistoryStorage> paccountHistoryDB;
std::unique_ptr<CBurnHistoryStorage> pburnHistoryDB;
