// Copyright (c) 2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <interfaces/chain.h>

#include <chain.h>
#include <chainparams.h>
#include <interfaces/handler.h>
#include <interfaces/wallet.h>
#include <net.h>
#include <node/coin.h>
#include <policy/mempool.h>
#include <policy/policy.h>
#include <policy/settings.h>
#include <primitives/block.h>
#include <primitives/blockhash.h>
#include <primitives/transaction.h>
#include <protocol.h>
#include <rpc/protocol.h>
#include <rpc/server.h>
#include <shutdown.h>
#include <sync.h>
#include <threadsafety.h>
#include <timedata.h>
#include <txmempool.h>
#include <ui_interface.h>
#include <univalue.h>
#include <util/system.h>
#include <validation.h>
#include <validationinterface.h>

#include <memory>
#include <utility>

namespace interfaces {
namespace {

    class LockImpl : public Chain::Lock, public UniqueLock<RecursiveMutex> {
        Optional<int> getHeight() override {
            int height = ::ChainActive().Height();
            if (height >= 0) {
                return height;
            }
            return nullopt;
        }
        Optional<int> getBlockHeight(const BlockHash &hash) override {
            CBlockIndex *block = LookupBlockIndex(hash);
            if (block && ::ChainActive().Contains(block)) {
                return block->nHeight;
            }
            return nullopt;
        }
        int getBlockDepth(const BlockHash &hash) override {
            const Optional<int> tip_height = getHeight();
            const Optional<int> height = getBlockHeight(hash);
            return tip_height && height ? *tip_height - *height + 1 : 0;
        }
        BlockHash getBlockHash(int height) override {
            CBlockIndex *block = ::ChainActive()[height];
            assert(block != nullptr);
            return block->GetBlockHash();
        }
        int64_t getBlockTime(int height) override {
            CBlockIndex *block = ::ChainActive()[height];
            assert(block != nullptr);
            return block->GetBlockTime();
        }
        int64_t getBlockMedianTimePast(int height) override {
            CBlockIndex *block = ::ChainActive()[height];
            assert(block != nullptr);
            return block->GetMedianTimePast();
        }
        bool haveBlockOnDisk(int height) override {
            CBlockIndex *block = ::ChainActive()[height];
            return block && (block->nStatus.hasData() != 0) && block->nTx > 0;
        }
        Optional<int> findFirstBlockWithTime(int64_t time,
                                             BlockHash *hash) override {
            CBlockIndex *block = ::ChainActive().FindEarliestAtLeast(time);
            if (block) {
                if (hash) {
                    *hash = block->GetBlockHash();
                }
                return block->nHeight;
            }
            return nullopt;
        }
        Optional<int> findFirstBlockWithTimeAndHeight(int64_t time,
                                                      int height) override {
            // TODO: Could update CChain::FindEarliestAtLeast() to take a height
            // parameter and use it with std::lower_bound() to make this
            // implementation more efficient and allow combining
            // findFirstBlockWithTime and findFirstBlockWithTimeAndHeight into
            // one method.
            for (CBlockIndex *block = ::ChainActive()[height]; block;
                 block = ::ChainActive().Next(block)) {
                if (block->GetBlockTime() >= time) {
                    return block->nHeight;
                }
            }
            return nullopt;
        }
        Optional<int> findPruned(int start_height,
                                 Optional<int> stop_height) override {
            if (::fPruneMode) {
                CBlockIndex *block = stop_height ? ::ChainActive()[*stop_height]
                                                 : ::ChainActive().Tip();
                while (block && block->nHeight >= start_height) {
                    if (block->nStatus.hasData() == 0) {
                        return block->nHeight;
                    }
                    block = block->pprev;
                }
            }
            return nullopt;
        }
        Optional<int> findFork(const BlockHash &hash,
                               Optional<int> *height) override {
            const CBlockIndex *block = LookupBlockIndex(hash);
            const CBlockIndex *fork =
                block ? ::ChainActive().FindFork(block) : nullptr;
            if (height) {
                if (block) {
                    *height = block->nHeight;
                } else {
                    height->reset();
                }
            }
            if (fork) {
                return fork->nHeight;
            }
            return nullopt;
        }
        bool isPotentialTip(const BlockHash &hash) override {
            if (::ChainActive().Tip()->GetBlockHash() == hash) {
                return true;
            }
            CBlockIndex *block = LookupBlockIndex(hash);
            return block && block->GetAncestor(::ChainActive().Height()) ==
                                ::ChainActive().Tip();
        }
        CBlockLocator getTipLocator() override {
            return ::ChainActive().GetLocator();
        }
        Optional<int> findLocatorFork(const CBlockLocator &locator) override {
            LockAnnotation lock(::cs_main);
            if (CBlockIndex *fork =
                    FindForkInGlobalIndex(::ChainActive(), locator)) {
                return fork->nHeight;
            }
            return nullopt;
        }
        bool contextualCheckTransactionForCurrentBlock(
            const Consensus::Params &params, const CTransaction &tx,
            CValidationState &state) override {
            LockAnnotation lock(::cs_main);
            return ContextualCheckTransactionForCurrentBlock(params, tx, state);
        }
        bool submitToMemoryPool(const Config &config, CTransactionRef tx,
                                Amount absurd_fee,
                                CValidationState &state) override {
            LockAnnotation lock(::cs_main);
            return AcceptToMemoryPool(config, ::g_mempool, state, tx,
                                      nullptr /* missing inputs */,
                                      false /* bypass limits */, absurd_fee);
        }

        using UniqueLock::UniqueLock;
    };

    class NotificationsHandlerImpl : public Handler, CValidationInterface {
    public:
        explicit NotificationsHandlerImpl(Chain &chain,
                                          Chain::Notifications &notifications)
            : m_chain(chain), m_notifications(&notifications) {
            RegisterValidationInterface(this);
        }
        ~NotificationsHandlerImpl() override { disconnect(); }
        void disconnect() override {
            if (m_notifications) {
                m_notifications = nullptr;
                UnregisterValidationInterface(this);
            }
        }
        void TransactionAddedToMempool(const CTransactionRef &tx) override {
            m_notifications->TransactionAddedToMempool(tx);
        }
        void TransactionRemovedFromMempool(const CTransactionRef &tx) override {
            m_notifications->TransactionRemovedFromMempool(tx);
        }
        void BlockConnected(
            const std::shared_ptr<const CBlock> &block,
            const CBlockIndex *index,
            const std::vector<CTransactionRef> &tx_conflicted) override {
            m_notifications->BlockConnected(*block, tx_conflicted);
        }
        void
        BlockDisconnected(const std::shared_ptr<const CBlock> &block) override {
            m_notifications->BlockDisconnected(*block);
        }
        void UpdatedBlockTip(const CBlockIndex *index,
                             const CBlockIndex *fork_index,
                             bool is_ibd) override {
            m_notifications->UpdatedBlockTip();
        }
        void ChainStateFlushed(const CBlockLocator &locator) override {
            m_notifications->ChainStateFlushed(locator);
        }
        Chain &m_chain;
        Chain::Notifications *m_notifications;
    };

    class RpcHandlerImpl : public Handler {
    public:
        RpcHandlerImpl(const CRPCCommand &command)
            : m_command(command), m_wrapped_command(&command) {
            m_command.actor = [this](Config &config,
                                     const JSONRPCRequest &request,
                                     UniValue &result, bool last_handler) {
                if (!m_wrapped_command) {
                    return false;
                }
                try {
                    return m_wrapped_command->actor(config, request, result,
                                                    last_handler);
                } catch (const UniValue &e) {
                    // If this is not the last handler and a wallet not found
                    // exception was thrown, return false so the next handler
                    // can try to handle the request. Otherwise, reraise the
                    // exception.
                    if (!last_handler) {
                        const UniValue &code = e["code"];
                        if (code.isNum() &&
                            code.get_int() == RPC_WALLET_NOT_FOUND) {
                            return false;
                        }
                    }
                    throw;
                }
            };
            ::tableRPC.appendCommand(m_command.name, &m_command);
        }

        void disconnect() override final {
            if (m_wrapped_command) {
                m_wrapped_command = nullptr;
                ::tableRPC.removeCommand(m_command.name, &m_command);
            }
        }

        ~RpcHandlerImpl() override { disconnect(); }

        CRPCCommand m_command;
        const CRPCCommand *m_wrapped_command;
    };

    class ChainImpl : public Chain {
    public:
        std::unique_ptr<Chain::Lock> lock(bool try_lock) override {
            auto lock = std::make_unique<LockImpl>(
                ::cs_main, "cs_main", __FILE__, __LINE__, try_lock);
            if (try_lock && lock && !*lock) {
                return {};
            }
            // Temporary to avoid CWG 1579
            std::unique_ptr<Chain::Lock> result = std::move(lock);
            return result;
        }
        bool findBlock(const BlockHash &hash, CBlock *block, int64_t *time,
                       int64_t *time_max) override {
            CBlockIndex *index;
            {
                LOCK(cs_main);
                index = LookupBlockIndex(hash);
                if (!index) {
                    return false;
                }
                if (time) {
                    *time = index->GetBlockTime();
                }
                if (time_max) {
                    *time_max = index->GetBlockTimeMax();
                }
            }
            if (block &&
                !ReadBlockFromDisk(*block, index, Params().GetConsensus())) {
                block->SetNull();
            }
            return true;
        }
        void findCoins(std::map<COutPoint, Coin> &coins) override {
            return FindCoins(coins);
        }
        double guessVerificationProgress(const BlockHash &block_hash) override {
            LOCK(cs_main);
            return GuessVerificationProgress(Params().TxData(),
                                             LookupBlockIndex(block_hash));
        }
        bool hasDescendantsInMempool(const TxId &txid) override {
            LOCK(::g_mempool.cs);
            auto it = ::g_mempool.GetIter(txid);
            return it && (*it)->GetCountWithDescendants() > 1;
        }
        void relayTransaction(const TxId &txid) override {
            CInv inv(MSG_TX, txid);
            g_connman->ForEachNode(
                [&inv](CNode *node) { node->PushInventory(inv); });
        }
        void getTransactionAncestry(const TxId &txid, size_t &ancestors,
                                    size_t &descendants) override {
            ::g_mempool.GetTransactionAncestry(txid, ancestors, descendants);
        }
        bool checkChainLimits(const CTransactionRef &tx) override {
            LockPoints lp;
            CTxMemPoolEntry entry(tx, Amount(), 0, 0, false, 0, lp);
            CTxMemPool::setEntries ancestors;
            auto limit_ancestor_count =
                gArgs.GetArg("-limitancestorcount", DEFAULT_ANCESTOR_LIMIT);
            auto limit_ancestor_size =
                gArgs.GetArg("-limitancestorsize",
                             DEFAULT_ANCESTOR_SIZE_LIMIT) *
                1000;
            auto limit_descendant_count =
                gArgs.GetArg("-limitdescendantcount", DEFAULT_DESCENDANT_LIMIT);
            auto limit_descendant_size =
                gArgs.GetArg("-limitdescendantsize",
                             DEFAULT_DESCENDANT_SIZE_LIMIT) *
                1000;
            std::string unused_error_string;
            LOCK(::g_mempool.cs);
            return ::g_mempool.CalculateMemPoolAncestors(
                entry, ancestors, limit_ancestor_count, limit_ancestor_size,
                limit_descendant_count, limit_descendant_size,
                unused_error_string);
        }
        CFeeRate estimateFee() const override {
            return ::g_mempool.estimateFee();
        }
        CFeeRate relayMinFee() override { return ::minRelayTxFee; }
        CFeeRate relayDustFee() override { return ::dustRelayFee; }
        Amount maxTxFee() override { return ::maxTxFee; }
        bool getPruneMode() override { return ::fPruneMode; }
        bool p2pEnabled() override { return g_connman != nullptr; }
        bool isReadyToBroadcast() override {
            return !::fImporting && !::fReindex && !isInitialBlockDownload();
        }
        bool isInitialBlockDownload() override {
            return ::ChainstateActive().IsInitialBlockDownload();
        }
        bool shutdownRequested() override { return ShutdownRequested(); }
        int64_t getAdjustedTime() override { return GetAdjustedTime(); }
        void initMessage(const std::string &message) override {
            ::uiInterface.InitMessage(message);
        }
        void initWarning(const std::string &message) override {
            InitWarning(message);
        }
        void initError(const std::string &message) override {
            InitError(message);
        }
        void loadWallet(std::unique_ptr<Wallet> wallet) override {
            ::uiInterface.LoadWallet(wallet);
        }
        void showProgress(const std::string &title, int progress,
                          bool resume_possible) override {
            ::uiInterface.ShowProgress(title, progress, resume_possible);
        }
        std::unique_ptr<Handler>
        handleNotifications(Notifications &notifications) override {
            return std::make_unique<NotificationsHandlerImpl>(*this,
                                                              notifications);
        }
        void waitForNotifications() override {
            SyncWithValidationInterfaceQueue();
        }
        std::unique_ptr<Handler>
        handleRpc(const CRPCCommand &command) override {
            return std::make_unique<RpcHandlerImpl>(command);
        }
    };

} // namespace

std::unique_ptr<Chain> MakeChain() {
    return std::make_unique<ChainImpl>();
}

} // namespace interfaces
