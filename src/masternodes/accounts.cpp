// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/accounts.h>

void CAccountsView::ForEachBalance(std::function<bool(CScript const &, CTokenAmount const &)> callback, BalanceKey const & start)
{
    ForEach<ByBalanceKey, BalanceKey, CAmount>([&callback] (BalanceKey const & key, CAmount val) {
        return callback(key.owner, CTokenAmount{key.tokenID, val});
    }, start);
}

CTokenAmount CAccountsView::GetBalance(CScript const & owner, DCT_ID tokenID) const
{
    CAmount val;
    bool ok = ReadBy<ByBalanceKey>(BalanceKey{owner, tokenID}, val);
    if (ok) {
        return CTokenAmount{tokenID, val};
    }
    return CTokenAmount{tokenID, 0};
}

Res CAccountsView::SetBalance(CScript const & owner, CTokenAmount amount)
{
    if (amount.nValue != 0) {
        WriteBy<ByBalanceKey>(BalanceKey{owner, amount.nTokenId}, amount.nValue);
    } else {
        EraseBy<ByBalanceKey>(BalanceKey{owner, amount.nTokenId});
    }
    return Res::Ok();
}

std::array<std::pair<CDoubleReason, CTokenAmount> > CAccountsView::GetDetailedBalance(CScript const & owner, DCT_ID tokenID) const
{
    std::array<std::pair<CDoubleReason, CTokenAmount> > val;
    bool ok = ReadBy<ByBalanceDetailsKey>(BalanceKey{owner, tokenID}, val);
    if (ok) {
        return val;
    }
    val.clear();
    return CTokenAmount{tokenID, val};
}

Res CAccountsView::SetDetailedBalance(CScript const & owner, std::array<std::pair<CDoubleReason, CTokenAmount> > amount)
{
    WriteBy<ByBalanceKey>(BalanceKey{owner, amount.nTokenId}, amount);
    return Res::Ok();
}

Res CAccountsView::AddBalance(CScript const & owner, CTokenAmount amount, CDoubleReason* reason)
{
    if (amount.nValue == 0) {
        return Res::Ok();
    }
    auto balance = GetBalance(owner, amount.nTokenId);
    auto res = balance.Add(amount.nValue);
    if (!res.ok) {
        return res;
    }

    if(reason) {
        auto balance = GetDetailedBalance(owner, amount.nTokenId);
        balance.push_back(std::make_pair<CDoubleReason, CTokenAmount>>(reason, amount.nValue));
        
        auto res2 = SetDetailedBalance(pwner, reason);
        if (!res2.ok) {
            return res;
        }
    }

    return SetBalance(owner, balance);
}

Res CAccountsView::SubBalance(CScript const & owner, CTokenAmount amount, CDoubleReason* reason)
{
    if (amount.nValue == 0) {
        return Res::Ok();
    }
    auto balance = GetBalance(owner, amount.nTokenId);
    auto res = balance.Sub(amount.nValue);
    if (!res.ok) {
        return res;
    }

    if(reason) {
        auto balance = GetDetailedBalance(owner, amount.nTokenId);
        balance.push_back(std::make_pair<CDoubleReason, CTokenAmount>>(reason, amount.nValue));
        
        auto res2 = SetDetailedBalance(pwner, reason);
        if (!res2.ok) {
            return res;
        }
    }

    return SetBalance(owner, balance);
}

Res CAccountsView::AddBalances(CScript const & owner, CBalances const & balances, CDoubleReason* reason)
{
    for (const auto& kv : balances.balances) {
        auto res = AddBalance(owner, CTokenAmount{kv.first, kv.second}, reason);
        if (!res.ok) {
            return res;
        }
    }
    return Res::Ok();
}

Res CAccountsView::SubBalances(CScript const & owner, CBalances const & balances, CDoubleReason* reason)
{
    for (const auto& kv : balances.balances) {
        auto res = SubBalance(owner, CTokenAmount{kv.first, kv.second}, reason);
        if (!res.ok) {
            return res;
        }
    }
    return Res::Ok();
}

void CAccountsView::ForEachAccount(std::function<bool(CScript const &)> callback, CScript const & start)
{
    ForEach<ByHeightKey, CScript, uint32_t>([&callback] (CScript const & owner, CLazySerialize<uint32_t>) {
        return callback(owner);
    }, start);
}

Res CAccountsView::UpdateBalancesHeight(CScript const & owner, uint32_t height)
{
    WriteBy<ByHeightKey>(owner, height);
    return Res::Ok();
}

uint32_t CAccountsView::GetBalancesHeight(CScript const & owner)
{
    uint32_t height;
    bool ok = ReadBy<ByHeightKey>(owner, height);
    return ok ? height : 0;
}

Res CAccountsView::StoreFuturesUserValues(const CFuturesUserKey& key, const CFuturesUserValue& futures)
{
    if (!WriteBy<ByFuturesSwapKey>(key, futures)) {
        return Res::Err("Failed to store futures");
    }

    return Res::Ok();
}

void CAccountsView::ForEachFuturesUserValues(std::function<bool(const CFuturesUserKey&, const CFuturesUserValue&)> callback, const CFuturesUserKey& start)
{
    ForEach<ByFuturesSwapKey, CFuturesUserKey, CFuturesUserValue>(callback, start);
}

Res CAccountsView::EraseFuturesUserValues(const CFuturesUserKey& key)
{
    if (!EraseBy<ByFuturesSwapKey>(key)) {
        return Res::Err("Failed to erase futures");
    }

    return Res::Ok();
}

Res CAccountsView::StoreFuturesDUSD(const CFuturesUserKey& key, const CAmount& amount)
{
    if (!WriteBy<ByFuturesDUSDKey>(key, amount)) {
        return Res::Err("Failed to store futures");
    }

    return Res::Ok();
}

void CAccountsView::ForEachFuturesDUSD(std::function<bool(const CFuturesUserKey&, const CAmount&)> callback, const CFuturesUserKey& start)
{
    ForEach<ByFuturesDUSDKey, CFuturesUserKey, CAmount>(callback, start);
}

Res CAccountsView::EraseFuturesDUSD(const CFuturesUserKey& key)
{
    if (!EraseBy<ByFuturesDUSDKey>(key)) {
        return Res::Err("Failed to erase futures");
    }

    return Res::Ok();
}
