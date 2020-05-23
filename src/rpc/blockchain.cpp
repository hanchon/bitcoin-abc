// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

<<<<<<< HEAD
#include "rpc/blockchain.h"

#include "amount.h"
#include "base58.h"
#include "chain.h"
#include "chainparams.h"
#include "checkpoints.h"
#include "coins.h"
#include "config.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "hash.h"
#include "policy/policy.h"
#include "primitives/transaction.h"
#include "rpc/server.h"
// TODO: are these needed?
#include "script/script.h"
#include "script/script_error.h"
#include "script/sign.h"
#include "script/standard.h"
#include "rpc/tojson.h"

#include "streams.h"
#include "sync.h"
#include "txdb.h"
#include "txmempool.h"
#include "util.h"
#include "utilstrencodings.h"
#include "validation.h"
#include "warnings.h"
=======
#include <rpc/blockchain.h>

#include <amount.h>
#include <chain.h>
#include <chainparams.h>
#include <checkpoints.h>
#include <coins.h>
#include <config.h>
#include <consensus/validation.h>
#include <core_io.h>
#include <hash.h>
#include <index/txindex.h>
#include <key_io.h>
#include <node/coinstats.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <script/descriptor.h>
#include <streams.h>
#include <sync.h>
#include <txdb.h>
#include <txmempool.h>
#include <util/strencodings.h>
#include <util/system.h>
#include <util/validation.h>
#include <validation.h>
#include <validationinterface.h>
#include <versionbitsinfo.h> // For VersionBitsDeploymentInfo
#include <warnings.h>
>>>>>>> abc/master

#include <boost/thread/thread.hpp> // boost::thread::interrupt

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>

struct CUpdatedBlock {
    uint256 hash;
    int height;
};

static Mutex cs_blockchange;
static std::condition_variable cond_blockchange;
static CUpdatedBlock latestblock;

/**
 * Calculate the difficulty for a given block index.
 */
double GetDifficulty(const CBlockIndex *blockindex) {
    assert(blockindex);

    int nShift = (blockindex->nBits >> 24) & 0xff;
    double dDiff = double(0x0000ffff) / double(blockindex->nBits & 0x00ffffff);

    while (nShift < 29) {
        dDiff *= 256.0;
        nShift++;
    }
    while (nShift > 29) {
        dDiff /= 256.0;
        nShift--;
    }

    return dDiff;
}

static int ComputeNextBlockAndDepth(const CBlockIndex *tip,
                                    const CBlockIndex *blockindex,
                                    const CBlockIndex *&next) {
    next = tip->GetAncestor(blockindex->nHeight + 1);
    if (next && next->pprev == blockindex) {
        return tip->nHeight - blockindex->nHeight + 1;
    }
    next = nullptr;
    return blockindex == tip ? 1 : -1;
}

UniValue blockheaderToJSON(const CBlockIndex *tip,
                           const CBlockIndex *blockindex) {
    UniValue result(UniValue::VOBJ);
    result.pushKV("hash", blockindex->GetBlockHash().GetHex());
    const CBlockIndex *pnext;
    int confirmations = ComputeNextBlockAndDepth(tip, blockindex, pnext);
    result.pushKV("confirmations", confirmations);
    result.pushKV("height", blockindex->nHeight);
    result.pushKV("version", blockindex->nVersion);
    result.pushKV("versionHex", strprintf("%08x", blockindex->nVersion));
    result.pushKV("merkleroot", blockindex->hashMerkleRoot.GetHex());
    result.pushKV("time", int64_t(blockindex->nTime));
    result.pushKV("mediantime", int64_t(blockindex->GetMedianTimePast()));
    result.pushKV("nonce", uint64_t(blockindex->nNonce));
    result.pushKV("bits", strprintf("%08x", blockindex->nBits));
    result.pushKV("difficulty", GetDifficulty(blockindex));
    result.pushKV("chainwork", blockindex->nChainWork.GetHex());
    result.pushKV("nTx", uint64_t(blockindex->nTx));

    if (blockindex->pprev) {
        result.pushKV("previousblockhash",
                      blockindex->pprev->GetBlockHash().GetHex());
    }
    if (pnext) {
        result.pushKV("nextblockhash", pnext->GetBlockHash().GetHex());
    }
    return result;
}
//TODO pass config param (?)
UniValue blockToDeltasJSON(const CBlock& block, const CBlockIndex* blockindex)
{
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("hash", block.GetHash().GetHex()));
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
    if (chainActive.Contains(blockindex)) {
        confirmations = chainActive.Height() - blockindex->nHeight + 1;
    } else {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block is an orphan");
    }
    result.push_back(Pair("confirmations", confirmations));
    result.push_back(Pair("size", (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION)));
    result.push_back(Pair("height", blockindex->nHeight));
    result.push_back(Pair("version", block.nVersion));
    result.push_back(Pair("merkleroot", block.hashMerkleRoot.GetHex()));

    UniValue deltas(UniValue::VARR);

    for (unsigned int i = 0; i < block.vtx.size(); i++) {
        const CTransaction &tx = *(block.vtx[i]);
        const uint256 txhash = tx.GetHash();

        UniValue entry(UniValue::VOBJ);
        entry.push_back(Pair("txid", txhash.GetHex()));
        entry.push_back(Pair("index", (int)i));

        UniValue inputs(UniValue::VARR);

        if (!tx.IsCoinBase()) {

            for (size_t j = 0; j < tx.vin.size(); j++) {
                const CTxIn input = tx.vin[j];

                UniValue delta(UniValue::VOBJ);

                CSpentIndexValue spentInfo;
                CSpentIndexKey spentKey(input.prevout.GetTxId(), input.prevout.GetN());

                if (GetSpentIndex(spentKey, spentInfo)) {
                    if (spentInfo.addressType == 1) {
                        delta.push_back(Pair("address", CBitcoinAddress(CKeyID(spentInfo.addressHash)).ToString()));
                    } else if (spentInfo.addressType == 2)  {
                        delta.push_back(Pair("address", CBitcoinAddress(CScriptID(spentInfo.addressHash)).ToString()));
                    } else {
                        continue;
                    }
                    delta.push_back(Pair("satoshis", -1 * spentInfo.satoshis));
                    delta.push_back(Pair("index", (int)j));
                    delta.push_back(Pair("prevtxid", input.prevout.GetTxId().GetHex()));
                    delta.push_back(Pair("prevout", (int)input.prevout.GetN()));

                    inputs.push_back(delta);
                } else {
                    throw JSONRPCError(RPC_INTERNAL_ERROR, "Spent information not available");
                }

            }
        }

        entry.push_back(Pair("inputs", inputs));

        UniValue outputs(UniValue::VARR);

        for (unsigned int k = 0; k < tx.vout.size(); k++) {
            const CTxOut &out = tx.vout[k];

            UniValue delta(UniValue::VOBJ);

            if (out.scriptPubKey.IsPayToScriptHash()) {
                std::vector<unsigned char> hashBytes(out.scriptPubKey.begin()+2, out.scriptPubKey.begin()+22);
                delta.push_back(Pair("address", CBitcoinAddress(CScriptID(uint160(hashBytes))).ToString()));

            } else if (out.scriptPubKey.IsPayToPublicKeyHash()) {
                std::vector<unsigned char> hashBytes(out.scriptPubKey.begin()+3, out.scriptPubKey.begin()+23);
                delta.push_back(Pair("address", CBitcoinAddress(CKeyID(uint160(hashBytes))).ToString()));
            } else {
                continue;
            }

            delta.push_back(Pair("satoshis", out.nValue / SATOSHI));
            delta.push_back(Pair("index", (int)k));

            outputs.push_back(delta);
        }

        entry.push_back(Pair("outputs", outputs));
        deltas.push_back(entry);

    }
    result.push_back(Pair("deltas", deltas));
    result.push_back(Pair("time", block.GetBlockTime()));
    result.push_back(Pair("mediantime", (int64_t)blockindex->GetMedianTimePast()));
    result.push_back(Pair("nonce", (uint64_t)block.nNonce));
    result.push_back(Pair("bits", strprintf("%08x", block.nBits)));
    result.push_back(Pair("difficulty", GetDifficulty(blockindex)));
    result.push_back(Pair("chainwork", blockindex->nChainWork.GetHex()));

    if (blockindex->pprev)
        result.push_back(Pair("previousblockhash", blockindex->pprev->GetBlockHash().GetHex()));
    CBlockIndex *pnext = chainActive.Next(blockindex);
    if (pnext)
        result.push_back(Pair("nextblockhash", pnext->GetBlockHash().GetHex()));
    return result;
}

UniValue blockToJSON(const CBlock &block, const CBlockIndex *tip,
                     const CBlockIndex *blockindex, bool txDetails) {
    UniValue result(UniValue::VOBJ);
    result.pushKV("hash", blockindex->GetBlockHash().GetHex());
    const CBlockIndex *pnext;
    int confirmations = ComputeNextBlockAndDepth(tip, blockindex, pnext);
    result.pushKV("confirmations", confirmations);
    result.pushKV("size", (int)::GetSerializeSize(block, PROTOCOL_VERSION));
    result.pushKV("height", blockindex->nHeight);
    result.pushKV("version", block.nVersion);
    result.pushKV("versionHex", strprintf("%08x", block.nVersion));
    result.pushKV("merkleroot", block.hashMerkleRoot.GetHex());
    UniValue txs(UniValue::VARR);
    for (const auto &tx : block.vtx) {
        if (txDetails) {
            UniValue objTx(UniValue::VOBJ);
            TxToUniv(*tx, uint256(), objTx, true, RPCSerializationFlags());
            txs.push_back(objTx);
        } else {
            txs.push_back(tx->GetId().GetHex());
        }
    }
    result.pushKV("tx", txs);
    result.pushKV("time", block.GetBlockTime());
    result.pushKV("mediantime", int64_t(blockindex->GetMedianTimePast()));
    result.pushKV("nonce", uint64_t(block.nNonce));
    result.pushKV("bits", strprintf("%08x", block.nBits));
    result.pushKV("difficulty", GetDifficulty(blockindex));
    result.pushKV("chainwork", blockindex->nChainWork.GetHex());
    result.pushKV("nTx", uint64_t(blockindex->nTx));

    if (blockindex->pprev) {
        result.pushKV("previousblockhash",
                      blockindex->pprev->GetBlockHash().GetHex());
    }
    if (pnext) {
        result.pushKV("nextblockhash", pnext->GetBlockHash().GetHex());
    }
    return result;
}

static UniValue getblockcount(const Config &config,
                              const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(RPCHelpMan{
            "getblockcount",
            "\nReturns the number of blocks in the longest blockchain.\n",
            {},
            RPCResult{"n    (numeric) The current block count\n"},
            RPCExamples{HelpExampleCli("getblockcount", "") +
                        HelpExampleRpc("getblockcount", "")},
        }
                                     .ToString());
    }

    LOCK(cs_main);
    return ::ChainActive().Height();
}

static UniValue getbestblockhash(const Config &config,
                                 const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(RPCHelpMan{
            "getbestblockhash",
            "\nReturns the hash of the best (tip) block in the "
            "longest blockchain.\n",
            {},
            RPCResult{"\"hex\"      (string) the block hash, hex-encoded\n"},
            RPCExamples{HelpExampleCli("getbestblockhash", "") +
                        HelpExampleRpc("getbestblockhash", "")},
        }
                                     .ToString());
    }

    LOCK(cs_main);
    return ::ChainActive().Tip()->GetBlockHash().GetHex();
}

UniValue getfinalizedblockhash(const Config &config,
                               const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(RPCHelpMan{
            "getfinalizedblockhash",
            "\nReturns the hash of the currently finalized block\n",
            {},
            RPCResult{"\"hex\"      (string) the block hash hex-encoded\n"},
            RPCExamples{HelpExampleCli("getfinalizedblockhash", "") +
                        HelpExampleRpc("getfinalizedblockhash", "")},
        }
                                     .ToString());
    }

    LOCK(cs_main);
    const CBlockIndex *blockIndexFinalized = GetFinalizedBlock();
    if (blockIndexFinalized) {
        return blockIndexFinalized->GetBlockHash().GetHex();
    }
    return UniValue(UniValue::VSTR);
}

void RPCNotifyBlockChange(bool ibd, const CBlockIndex *pindex) {
    if (pindex) {
        std::lock_guard<std::mutex> lock(cs_blockchange);
        latestblock.hash = pindex->GetBlockHash();
        latestblock.height = pindex->nHeight;
    }
    cond_blockchange.notify_all();
}

static UniValue waitfornewblock(const Config &config,
                                const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() > 1) {
        throw std::runtime_error(RPCHelpMan{
            "waitfornewblock",
            "\nWaits for a specific new block and returns useful "
            "info about it.\n"
            "\nReturns the current block on timeout or exit.\n",
            {
                {"timeout", RPCArg::Type::NUM, /* default */ "0",
                 "Time in milliseconds to wait for a response. 0 indicates no "
                 "timeout."},
            },
            RPCResult{"{                           (json object)\n"
                      "  \"hash\" : {       (string) The blockhash\n"
                      "  \"height\" : {     (int) Block height\n"
                      "}\n"},
            RPCExamples{HelpExampleCli("waitfornewblock", "1000") +
                        HelpExampleRpc("waitfornewblock", "1000")},
        }
                                     .ToString());
    }
    int timeout = 0;
    if (!request.params[0].isNull()) {
        timeout = request.params[0].get_int();
    }

    CUpdatedBlock block;
    {
        WAIT_LOCK(cs_blockchange, lock);
        block = latestblock;
        if (timeout) {
            cond_blockchange.wait_for(
                lock, std::chrono::milliseconds(timeout), [&block] {
                    return latestblock.height != block.height ||
                           latestblock.hash != block.hash || !IsRPCRunning();
                });
        } else {
            cond_blockchange.wait(lock, [&block] {
                return latestblock.height != block.height ||
                       latestblock.hash != block.hash || !IsRPCRunning();
            });
        }
        block = latestblock;
    }
    UniValue ret(UniValue::VOBJ);
    ret.pushKV("hash", block.hash.GetHex());
    ret.pushKV("height", block.height);
    return ret;
}

static UniValue waitforblock(const Config &config,
                             const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() < 1 ||
        request.params.size() > 2) {
        throw std::runtime_error(RPCHelpMan{
            "waitforblock",
            "\nWaits for a specific new block and returns useful "
            "info about it.\n"
            "\nReturns the current block on timeout or exit.\n",
            {
                {"blockhash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO,
                 "Block hash to wait for."},
                {"timeout", RPCArg::Type::NUM, /* default */ "0",
                 "Time in milliseconds to wait for a response. 0 "
                 "indicates no timeout."},
            },
            RPCResult{"{                           (json object)\n"
                      "  \"hash\" : {       (string) The blockhash\n"
                      "  \"height\" : {     (int) Block height\n"
                      "}\n"},
            RPCExamples{HelpExampleCli("waitforblock",
                                       "\"0000000000079f8ef3d2c688c244eb7a4"
                                       "570b24c9ed7b4a8c619eb02596f8862\", "
                                       "1000") +
                        HelpExampleRpc("waitforblock",
                                       "\"0000000000079f8ef3d2c688c244eb7a4"
                                       "570b24c9ed7b4a8c619eb02596f8862\", "
                                       "1000")},
        }
                                     .ToString());
    }

    int timeout = 0;

    BlockHash hash(ParseHashV(request.params[0], "blockhash"));

    if (!request.params[1].isNull()) {
        timeout = request.params[1].get_int();
    }

    CUpdatedBlock block;
    {
        WAIT_LOCK(cs_blockchange, lock);
        if (timeout) {
            cond_blockchange.wait_for(
                lock, std::chrono::milliseconds(timeout), [&hash] {
                    return latestblock.hash == hash || !IsRPCRunning();
                });
        } else {
            cond_blockchange.wait(lock, [&hash] {
                return latestblock.hash == hash || !IsRPCRunning();
            });
        }
        block = latestblock;
    }

    UniValue ret(UniValue::VOBJ);
    ret.pushKV("hash", block.hash.GetHex());
    ret.pushKV("height", block.height);
    return ret;
}

static UniValue waitforblockheight(const Config &config,
                                   const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() < 1 ||
        request.params.size() > 2) {
        throw std::runtime_error(RPCHelpMan{
            "waitforblockheight",
            "\nWaits for (at least) block height and returns the "
            "height and hash\nof the current tip.\n"
            "\nReturns the current block on timeout or exit.\n",
            {
                {"height", RPCArg::Type::NUM, RPCArg::Optional::NO,
                 "Block height to wait for."},
                {"timeout", RPCArg::Type::NUM, /* default */ "0",
                 "Time in milliseconds to wait for a response. 0 "
                 "indicates no timeout."},
            },
            RPCResult{"{                           (json object)\n"
                      "  \"hash\" : {       (string) The blockhash\n"
                      "  \"height\" : {     (int) Block height\n"
                      "}\n"},
            RPCExamples{HelpExampleCli("waitforblockheight", "\"100\", 1000") +
                        HelpExampleRpc("waitforblockheight", "\"100\", 1000")},
        }
                                     .ToString());
    }

    int timeout = 0;

    int height = request.params[0].get_int();

    if (!request.params[1].isNull()) {
        timeout = request.params[1].get_int();
    }

    CUpdatedBlock block;
    {
        WAIT_LOCK(cs_blockchange, lock);
        if (timeout) {
            cond_blockchange.wait_for(
                lock, std::chrono::milliseconds(timeout), [&height] {
                    return latestblock.height >= height || !IsRPCRunning();
                });
        } else {
            cond_blockchange.wait(lock, [&height] {
                return latestblock.height >= height || !IsRPCRunning();
            });
        }
        block = latestblock;
    }
    UniValue ret(UniValue::VOBJ);
    ret.pushKV("hash", block.hash.GetHex());
    ret.pushKV("height", block.height);
    return ret;
}

static UniValue
syncwithvalidationinterfacequeue(const Config &config,
                                 const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() > 0) {
        throw std::runtime_error(RPCHelpMan{
            "syncwithvalidationinterfacequeue",
            "\nWaits for the validation interface queue to catch up on "
            "everything that was there when we entered this function.\n",
            {},
            RPCResults{},
            RPCExamples{HelpExampleCli("syncwithvalidationinterfacequeue", "") +
                        HelpExampleRpc("syncwithvalidationinterfacequeue", "")},
        }
                                     .ToString());
    }
    SyncWithValidationInterfaceQueue();
    return NullUniValue;
}

static UniValue getdifficulty(const Config &config,
                              const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(RPCHelpMan{
            "getdifficulty",
            "\nReturns the proof-of-work difficulty as a multiple of the "
            "minimum difficulty.\n",
            {},
            RPCResult{"n.nnn       (numeric) the proof-of-work difficulty as a "
                      "multiple of the minimum difficulty.\n"},
            RPCExamples{HelpExampleCli("getdifficulty", "") +
                        HelpExampleRpc("getdifficulty", "")},
        }
                                     .ToString());
    }

    LOCK(cs_main);
    return GetDifficulty(::ChainActive().Tip());
}

static std::string EntryDescriptionString() {
    return "    \"size\" : n,             (numeric) transaction size.\n"
           "    \"fee\" : n,              (numeric) transaction fee in " +
           CURRENCY_UNIT + "(DEPRECATED)" +
           "\n"
           "    \"modifiedfee\" : n,      (numeric) transaction fee with fee "
           "deltas used for mining priority (DEPRECATED)\n"
           "    \"time\" : n,             (numeric) local time transaction "
           "entered pool in seconds since 1 Jan 1970 GMT\n"
           "    \"height\" : n,           (numeric) block height when "
           "transaction entered pool\n"
           "    \"descendantcount\" : n,  (numeric) number of in-mempool "
           "descendant transactions (including this one)\n"
           "    \"descendantsize\" : n,   (numeric) transaction size "
           "of in-mempool descendants (including this one)\n"
           "    \"descendantfees\" : n,   (numeric) modified fees (see above) "
           "of in-mempool descendants (including this one) (DEPRECATED)\n"
           "    \"ancestorcount\" : n,    (numeric) number of in-mempool "
           "ancestor transactions (including this one)\n"
           "    \"ancestorsize\" : n,     (numeric) transaction size "
           "of in-mempool ancestors (including this one)\n"
           "    \"ancestorfees\" : n,     (numeric) modified fees (see above) "
           "of in-mempool ancestors (including this one) (DEPRECATED)\n"
           "    \"fees\" : {\n"
           "        \"base\" : n,         (numeric) transaction fee in " +
           CURRENCY_UNIT +
           "\n"
           "        \"modified\" : n,     (numeric) transaction fee with fee "
           "deltas used for mining priority in " +
           CURRENCY_UNIT +
           "\n"
           "        \"ancestor\" : n,     (numeric) modified fees (see above) "
           "of in-mempool ancestors (including this one) in " +
           CURRENCY_UNIT +
           "\n"
           "        \"descendant\" : n,   (numeric) modified fees (see above) "
           "of in-mempool descendants (including this one) in " +
           CURRENCY_UNIT +
           "\n"
           "    }\n"
           "    \"depends\" : [           (array) unconfirmed transactions "
           "used as inputs for this transaction\n"
           "        \"transactionid\",    (string) parent transaction id\n"
           "       ... ]\n"
           "    \"spentby\" : [           (array) unconfirmed transactions "
           "spending outputs from this transaction\n"
           "        \"transactionid\",    (string) child transaction id\n"
           "       ... ]\n";
}

static void entryToJSON(const CTxMemPool &pool, UniValue &info,
                        const CTxMemPoolEntry &e)
    EXCLUSIVE_LOCKS_REQUIRED(pool.cs) {
    AssertLockHeld(pool.cs);

    UniValue fees(UniValue::VOBJ);
    fees.pushKV("base", ValueFromAmount(e.GetFee()));
    fees.pushKV("modified", ValueFromAmount(e.GetModifiedFee()));
    fees.pushKV("ancestor", ValueFromAmount(e.GetModFeesWithAncestors()));
    fees.pushKV("descendant", ValueFromAmount(e.GetModFeesWithDescendants()));
    info.pushKV("fees", fees);

    info.pushKV("size", (int)e.GetTxSize());
    info.pushKV("fee", ValueFromAmount(e.GetFee()));
    info.pushKV("modifiedfee", ValueFromAmount(e.GetModifiedFee()));
    info.pushKV("time", e.GetTime());
    info.pushKV("height", (int)e.GetHeight());
    info.pushKV("descendantcount", e.GetCountWithDescendants());
    info.pushKV("descendantsize", e.GetSizeWithDescendants());
    info.pushKV("descendantfees", e.GetModFeesWithDescendants() / SATOSHI);
    info.pushKV("ancestorcount", e.GetCountWithAncestors());
    info.pushKV("ancestorsize", e.GetSizeWithAncestors());
    info.pushKV("ancestorfees", e.GetModFeesWithAncestors() / SATOSHI);
    const CTransaction &tx = e.GetTx();
    std::set<std::string> setDepends;
    for (const CTxIn &txin : tx.vin) {
        if (pool.exists(txin.prevout.GetTxId())) {
            setDepends.insert(txin.prevout.GetTxId().ToString());
        }
    }

    UniValue depends(UniValue::VARR);
    for (const std::string &dep : setDepends) {
        depends.push_back(dep);
    }

    info.pushKV("depends", depends);

    UniValue spent(UniValue::VARR);
    const CTxMemPool::txiter &it = pool.mapTx.find(tx.GetId());
    const CTxMemPool::setEntries &setChildren = pool.GetMemPoolChildren(it);
    for (CTxMemPool::txiter childiter : setChildren) {
        spent.push_back(childiter->GetTx().GetId().ToString());
    }

    info.pushKV("spentby", spent);
}

UniValue MempoolToJSON(const CTxMemPool &pool, bool verbose) {
    if (verbose) {
        LOCK(pool.cs);
        UniValue o(UniValue::VOBJ);
        for (const CTxMemPoolEntry &e : pool.mapTx) {
            const uint256 &txid = e.GetTx().GetId();
            UniValue info(UniValue::VOBJ);
            entryToJSON(pool, info, e);
            // Mempool has unique entries so there is no advantage in using
            // UniValue::pushKV, which checks if the key already exists in O(N).
            // UniValue::__pushKV is used instead which currently is O(1).
            o.__pushKV(txid.ToString(), info);
        }
        return o;
    } else {
        std::vector<uint256> vtxids;
        pool.queryHashes(vtxids);

        UniValue a(UniValue::VARR);
        for (const uint256 &txid : vtxids) {
            a.push_back(txid.ToString());
        }

        return a;
    }
}

static UniValue getrawmempool(const Config &config,
                              const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() > 1) {
        throw std::runtime_error(RPCHelpMan{
            "getrawmempool",
            "\nReturns all transaction ids in memory pool as a json "
            "array of string transaction ids.\n"
            "\nHint: use getmempoolentry to fetch a specific "
            "transaction from the mempool.\n",
            {
                {"verbose", RPCArg::Type::BOOL, /* default */ "false",
                 "True for a json object, false for array of "
                 "transaction ids"},
            },
            RPCResult{"for verbose = false",
                      "[                     (json array of string)\n"
                      "  \"transactionid\"     (string) The transaction id\n"
                      "  ,...\n"
                      "]\n"
                      "\nResult: (for verbose = true):\n"
                      "{                           (json object)\n"
                      "  \"transactionid\" : {       (json object)\n" +
                          EntryDescriptionString() +
                          "  }, ...\n"
                          "}\n"},
            RPCExamples{HelpExampleCli("getrawmempool", "true") +
                        HelpExampleRpc("getrawmempool", "true")},
        }
                                     .ToString());
    }

    bool fVerbose = false;
    if (!request.params[0].isNull()) {
        fVerbose = request.params[0].get_bool();
    }

    return MempoolToJSON(::g_mempool, fVerbose);
}

static UniValue getmempoolancestors(const Config &config,
                                    const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() < 1 ||
        request.params.size() > 2) {
        throw std::runtime_error(RPCHelpMan{
            "getmempoolancestors",
            "\nIf txid is in the mempool, returns all in-mempool "
            "ancestors.\n",
            {
                {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO,
                 "The transaction id (must be in mempool)"},
                {"verbose", RPCArg::Type::BOOL, /* default */ "false",
                 "True for a json object, false for array of "
                 "transaction ids"},
            },
            {
                RPCResult{
                    "for verbose = false",
                    "[                       (json array of strings)\n"
                    "  \"transactionid\"           (string) The transaction id "
                    "of an in-mempool ancestor transaction\n"
                    "  ,...\n"
                    "]\n"},
                RPCResult{"for verbose = true",
                          "{                           (json object)\n"
                          "  \"transactionid\" : {       (json object)\n" +
                              EntryDescriptionString() +
                              "  }, ...\n"
                              "}\n"},
            },
            RPCExamples{HelpExampleCli("getmempoolancestors", "\"mytxid\"") +
                        HelpExampleRpc("getmempoolancestors", "\"mytxid\"")},
        }
                                     .ToString());
    }

    bool fVerbose = false;
    if (!request.params[1].isNull()) {
        fVerbose = request.params[1].get_bool();
    }

    TxId txid(ParseHashV(request.params[0], "parameter 1"));

    LOCK(g_mempool.cs);

    CTxMemPool::txiter it = g_mempool.mapTx.find(txid);
    if (it == g_mempool.mapTx.end()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                           "Transaction not in mempool");
    }

    CTxMemPool::setEntries setAncestors;
    uint64_t noLimit = std::numeric_limits<uint64_t>::max();
    std::string dummy;
    g_mempool.CalculateMemPoolAncestors(*it, setAncestors, noLimit, noLimit,
                                        noLimit, noLimit, dummy, false);

    if (!fVerbose) {
        UniValue o(UniValue::VARR);
        for (CTxMemPool::txiter ancestorIt : setAncestors) {
            o.push_back(ancestorIt->GetTx().GetId().ToString());
        }

        return o;
    } else {
        UniValue o(UniValue::VOBJ);
        for (CTxMemPool::txiter ancestorIt : setAncestors) {
            const CTxMemPoolEntry &e = *ancestorIt;
            const TxId &_txid = e.GetTx().GetId();
            UniValue info(UniValue::VOBJ);
            entryToJSON(::g_mempool, info, e);
            o.pushKV(_txid.ToString(), info);
        }
        return o;
    }
}

static UniValue getmempooldescendants(const Config &config,
                                      const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() < 1 ||
        request.params.size() > 2) {
        throw std::runtime_error(RPCHelpMan{
            "getmempooldescendants",
            "\nIf txid is in the mempool, returns all in-mempool "
            "descendants.\n",
            {
                {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO,
                 "The transaction id (must be in mempool)"},
                {"verbose", RPCArg::Type::BOOL, /* default */ "false",
                 "True for a json object, false for array of "
                 "transaction ids"},
            },
            {
                RPCResult{
                    "for verbose = false",
                    "[                       (json array of strings)\n"
                    "  \"transactionid\"           (string) The transaction id "
                    "of an in-mempool descendant transaction\n"
                    "  ,...\n"
                    "]\n"},
                RPCResult{"for verbose = true",
                          "{                           (json object)\n"
                          "  \"transactionid\" : {       (json object)\n" +
                              EntryDescriptionString() +
                              "  }, ...\n"
                              "}\n"},
            },
            RPCExamples{HelpExampleCli("getmempooldescendants", "\"mytxid\"") +
                        HelpExampleRpc("getmempooldescendants", "\"mytxid\"")},
        }
                                     .ToString());
    }

    bool fVerbose = false;
    if (!request.params[1].isNull()) {
        fVerbose = request.params[1].get_bool();
    }

    TxId txid(ParseHashV(request.params[0], "parameter 1"));

    LOCK(g_mempool.cs);

    CTxMemPool::txiter it = g_mempool.mapTx.find(txid);
    if (it == g_mempool.mapTx.end()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                           "Transaction not in mempool");
    }

    CTxMemPool::setEntries setDescendants;
    g_mempool.CalculateDescendants(it, setDescendants);
    // CTxMemPool::CalculateDescendants will include the given tx
    setDescendants.erase(it);

    if (!fVerbose) {
        UniValue o(UniValue::VARR);
        for (CTxMemPool::txiter descendantIt : setDescendants) {
            o.push_back(descendantIt->GetTx().GetId().ToString());
        }

        return o;
    } else {
        UniValue o(UniValue::VOBJ);
        for (CTxMemPool::txiter descendantIt : setDescendants) {
            const CTxMemPoolEntry &e = *descendantIt;
            const TxId &_txid = e.GetTx().GetId();
            UniValue info(UniValue::VOBJ);
            entryToJSON(::g_mempool, info, e);
            o.pushKV(_txid.ToString(), info);
        }
        return o;
    }
}

static UniValue getmempoolentry(const Config &config,
                                const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(RPCHelpMan{
            "getmempoolentry",
            "\nReturns mempool data for given transaction\n",
            {
                {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO,
                 "The transaction id (must be in mempool)"},
            },
            RPCResult{"{                           (json object)\n" +
                      EntryDescriptionString() + "}\n"},
            RPCExamples{HelpExampleCli("getmempoolentry", "\"mytxid\"") +
                        HelpExampleRpc("getmempoolentry", "\"mytxid\"")},
        }
                                     .ToString());
    }

    TxId txid(ParseHashV(request.params[0], "parameter 1"));

    LOCK(g_mempool.cs);

    CTxMemPool::txiter it = g_mempool.mapTx.find(txid);
    if (it == g_mempool.mapTx.end()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                           "Transaction not in mempool");
    }

    const CTxMemPoolEntry &e = *it;
    UniValue info(UniValue::VOBJ);
    entryToJSON(::g_mempool, info, e);
    return info;
}

static UniValue getblockdeltas(const Config &config, const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error("");

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hash];

    if (fHavePruned && !(pblockindex->nStatus.hasData()) && pblockindex->nTx > 0)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Block not available (pruned data)");

    if(!ReadBlockFromDisk(block, pblockindex, config))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");

    return blockToDeltasJSON(block, pblockindex);
}

static UniValue getblockhashes(const Config &config, const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2)
        throw std::runtime_error(
            "getblockhashes timestamp\n"
            "\nReturns array of hashes of blocks within the timestamp range provided.\n"
            "\nArguments:\n"
            "1. high         (numeric, required) The newer block timestamp\n"
            "2. low          (numeric, required) The older block timestamp\n"
            "3. options      (string, required) A json object\n"
            "    {\n"
            "      \"noOrphans\":true   (boolean) will only include blocks on the main chain\n"
            "      \"logicalTimes\":true   (boolean) will include logical timestamps with hashes\n"
            "    }\n"
            "\nResult:\n"
            "[\n"
            "  \"hash\"         (string) The block hash\n"
            "]\n"
            "[\n"
            "  {\n"
            "    \"blockhash\": (string) The block hash\n"
            "    \"logicalts\": (numeric) The logical timestamp\n"
            "  }\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockhashes", "1231614698 1231024505")
            + HelpExampleRpc("getblockhashes", "1231614698, 1231024505")
            + HelpExampleCli("getblockhashes", "1231614698 1231024505 '{\"noOrphans\":false, \"logicalTimes\":true}'")
            );

    unsigned int high = request.params[0].get_int();
    unsigned int low = request.params[1].get_int();
    bool fActiveOnly = false;
    bool fLogicalTS = false;

    if (request.params.size() > 2) {
        if (request.params[2].isObject()) {
            UniValue noOrphans = find_value(request.params[2].get_obj(), "noOrphans");
            UniValue returnLogical = find_value(request.params[2].get_obj(), "logicalTimes");

            if (noOrphans.isBool())
                fActiveOnly = noOrphans.get_bool();

            if (returnLogical.isBool())
                fLogicalTS = returnLogical.get_bool();
        }
    }

    std::vector<std::pair<uint256, unsigned int> > blockHashes;

    if (fActiveOnly)
        LOCK(cs_main);

    if (!GetTimestampIndex(high, low, fActiveOnly, blockHashes)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for block hashes");
    }

    UniValue result(UniValue::VARR);

    for (std::vector<std::pair<uint256, unsigned int> >::const_iterator it=blockHashes.begin(); it!=blockHashes.end(); it++) {
        if (fLogicalTS) {
            UniValue item(UniValue::VOBJ);
            item.push_back(Pair("blockhash", it->first.GetHex()));
            item.push_back(Pair("logicalts", (int)it->second));
            result.push_back(item);
        } else {
            result.push_back(it->first.GetHex());
        }
    }

    return result;
}

static UniValue getblockhash(const Config &config, const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(RPCHelpMan{
            "getblockhash",
            "\nReturns hash of block in best-block-chain at height "
            "provided.\n",
            {
                {"height", RPCArg::Type::NUM, RPCArg::Optional::NO,
                 "The height index"},
            },
            RPCResult{"\"hash\"         (string) The block hash\n"},
            RPCExamples{HelpExampleCli("getblockhash", "1000") +
                        HelpExampleRpc("getblockhash", "1000")},
        }
                                     .ToString());
    }

    LOCK(cs_main);

    int nHeight = request.params[0].get_int();
    if (nHeight < 0 || nHeight > ::ChainActive().Height()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Block height out of range");
    }

    CBlockIndex *pblockindex = ::ChainActive()[nHeight];
    return pblockindex->GetBlockHash().GetHex();
}

static UniValue getblockheader(const Config &config,
                               const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() < 1 ||
        request.params.size() > 2) {
        throw std::runtime_error(RPCHelpMan{
            "getblockheader",
            "\nIf verbose is false, returns a string that is "
            "serialized, hex-encoded data for blockheader 'hash'.\n"
            "If verbose is true, returns an Object with information "
            "about blockheader <hash>.\n",
            {
                {"blockhash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO,
                 "The block hash"},
                {"verbose", RPCArg::Type::BOOL, /* default */ "true",
                 "true for a json object, false for the hex-encoded data"},
            },
            {
                RPCResult{
                    "for verbose = true",
                    "{\n"
                    "  \"hash\" : \"hash\",     (string) the block hash (same "
                    "as provided)\n"
                    "  \"confirmations\" : n,   (numeric) The number of "
                    "confirmations, or -1 if the block is not on the main "
                    "chain\n"
                    "  \"height\" : n,          (numeric) The block height or "
                    "index\n"
                    "  \"version\" : n,         (numeric) The block version\n"
                    "  \"versionHex\" : \"00000000\", (string) The block "
                    "version formatted in hexadecimal\n"
                    "  \"merkleroot\" : \"xxxx\", (string) The merkle root\n"
                    "  \"time\" : ttt,          (numeric) The block time in "
                    "seconds since epoch (Jan 1 1970 GMT)\n"
                    "  \"mediantime\" : ttt,    (numeric) The median block "
                    "time in seconds since epoch (Jan 1 1970 GMT)\n"
                    "  \"nonce\" : n,           (numeric) The nonce\n"
                    "  \"bits\" : \"1d00ffff\", (string) The bits\n"
                    "  \"difficulty\" : x.xxx,  (numeric) The difficulty\n"
                    "  \"chainwork\" : \"0000...1f3\"     (string) Expected "
                    "number of hashes required to produce the current chain "
                    "(in hex)\n"
                    "  \"nTx\" : n,             (numeric) The number of "
                    "transactions in the block.\n"
                    "  \"previousblockhash\" : \"hash\",  (string) The hash of "
                    "the previous block\n"
                    "  \"nextblockhash\" : \"hash\",      (string) The hash of "
                    "the next block\n"
                    "}\n"},
                RPCResult{"for verbose=false",
                          "\"data\"             (string) A string that is "
                          "serialized, hex-encoded data for block 'hash'.\n"},
            },
            RPCExamples{HelpExampleCli("getblockheader",
                                       "\"00000000c937983704a73af28acdec3"
                                       "7b049d214adbda81d7e2a3dd146f6ed09"
                                       "\"") +
                        HelpExampleRpc("getblockheader",
                                       "\"00000000c937983704a73af28acdec3"
                                       "7b049d214adbda81d7e2a3dd146f6ed09"
                                       "\"")},
        }
                                     .ToString());
    }

    BlockHash hash(ParseHashV(request.params[0], "hash"));

    bool fVerbose = true;
    if (!request.params[1].isNull()) {
        fVerbose = request.params[1].get_bool();
    }

    const CBlockIndex *pblockindex;
    const CBlockIndex *tip;
    {
        LOCK(cs_main);
        pblockindex = LookupBlockIndex(hash);
        tip = ::ChainActive().Tip();
    }

    if (!pblockindex) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
    }

    if (!fVerbose) {
        CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
        ssBlock << pblockindex->GetBlockHeader();
        std::string strHex = HexStr(ssBlock.begin(), ssBlock.end());
        return strHex;
    }

    return blockheaderToJSON(tip, pblockindex);
}

static CBlock GetBlockChecked(const Config &config,
                              const CBlockIndex *pblockindex) {
    CBlock block;
    if (IsBlockPruned(pblockindex)) {
        throw JSONRPCError(RPC_MISC_ERROR, "Block not available (pruned data)");
    }

    if (!ReadBlockFromDisk(block, pblockindex,
                           config.GetChainParams().GetConsensus())) {
        // Block not found on disk. This could be because we have the block
        // header in our index but don't have the block (for example if a
        // non-whitelisted node sends us an unrequested long chain of valid
        // blocks, we add the headers to our index, but don't accept the block).
        throw JSONRPCError(RPC_MISC_ERROR, "Block not found on disk");
    }

    return block;
}

static UniValue getblock(const Config &config, const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() < 1 ||
        request.params.size() > 2) {
        throw std::runtime_error(RPCHelpMan{
            "getblock",
            "\nIf verbosity is 0 or false, returns a string that is "
            "serialized, hex-encoded data for block 'hash'.\n"
            "If verbosity is 1 or true, returns an Object with information "
            "about block <hash>.\n"
            "If verbosity is 2, returns an Object with information about "
            "block <hash> and information about each transaction.\n",
            {
                {"blockhash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO,
                 "The block hash"},
                {"verbosity", RPCArg::Type::NUM, /* default */ "1",
                 "0 for hex-encoded data, 1 for a json object, and 2 for "
                 "json object with transaction data"},
            },
            {
                RPCResult{
                    "for verbosity = 0",
                    "\"data\"                   (string) A string that is "
                    "serialized, hex-encoded data for block 'hash'.\n"},
                RPCResult{
                    "for verbosity = 1",
                    "{\n"
                    "  \"hash\" : \"hash\",       (string) The block hash "
                    "(same as provided)\n"
                    "  \"confirmations\" : n,   (numeric) The number of "
                    "confirmations, or -1 if the block is not on the main "
                    "chain\n"
                    "  \"size\" : n,            (numeric) The block size\n"
                    "  \"height\" : n,          (numeric) The block height or "
                    "index\n"
                    "  \"version\" : n,         (numeric) The block version\n"
                    "  \"versionHex\" : \"00000000\", (string) The block "
                    "version formatted in hexadecimal\n"
                    "  \"merkleroot\" : \"xxxx\", (string) The merkle root\n"
                    "  \"tx\" : [               (array of string) The "
                    "transaction ids\n"
                    "     \"transactionid\"     (string) The transaction id\n"
                    "     ,...\n"
                    "  ],\n"
                    "  \"time\" : ttt,          (numeric) The block time in "
                    "seconds since epoch (Jan 1 1970 GMT)\n"
                    "  \"mediantime\" : ttt,    (numeric) The median block "
                    "time in seconds since epoch (Jan 1 1970 GMT)\n"
                    "  \"nonce\" : n,           (numeric) The nonce\n"
                    "  \"bits\" : \"1d00ffff\",   (string) The bits\n"
                    "  \"difficulty\" : x.xxx,  (numeric) The difficulty\n"
                    "  \"chainwork\" : \"xxxx\",  (string) Expected number of "
                    "hashes required to produce the chain up to this block (in "
                    "hex)\n"
                    "  \"nTx\" : n,             (numeric) The number of "
                    "transactions in the block.\n"
                    "  \"previousblockhash\" : \"hash\",  (string) The hash of "
                    "the previous block\n"
                    "  \"nextblockhash\" : \"hash\"       (string) The hash of "
                    "the next block\n"
                    "}\n"},
                RPCResult{
                    "for verbosity = 2",
                    "{\n"
                    "  ...,                   Same output as verbosity = 1\n"
                    "  \"tx\" : [               (array of Objects) The "
                    "transactions in the format of the getrawtransaction RPC; "
                    "different from verbosity = 1 \"tx\" result\n"
                    "    ...\n"
                    "  ],\n"
                    "  ...                    Same output as verbosity = 1\n"
                    "}\n"},
            },
            RPCExamples{HelpExampleCli("getblock",
                                       "\"00000000c937983704a73af28acdec37b049d"
                                       "214adbda81d7e2a3dd146f6ed09\"") +
                        HelpExampleRpc("getblock",
                                       "\"00000000c937983704a73af28acdec37b049d"
                                       "214adbda81d7e2a3dd146f6ed09\"")},
        }
                                     .ToString());
    }

    LOCK(cs_main);

    BlockHash hash(ParseHashV(request.params[0], "blockhash"));

    int verbosity = 1;
    if (!request.params[1].isNull()) {
        if (request.params[1].isNum()) {
            verbosity = request.params[1].get_int();
        } else {
            verbosity = request.params[1].get_bool() ? 1 : 0;
        }
    }

    const CBlockIndex *pblockindex = LookupBlockIndex(hash);
    if (!pblockindex) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
    }

    const CBlock block = GetBlockChecked(config, pblockindex);

    if (verbosity <= 0) {
        CDataStream ssBlock(SER_NETWORK,
                            PROTOCOL_VERSION | RPCSerializationFlags());
        ssBlock << block;
        std::string strHex = HexStr(ssBlock.begin(), ssBlock.end());
        return strHex;
    }

    return blockToJSON(block, ::ChainActive().Tip(), pblockindex,
                       verbosity >= 2);
}

static UniValue pruneblockchain(const Config &config,
                                const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(RPCHelpMan{
            "pruneblockchain",
            "",
            {
                {"height", RPCArg::Type::NUM, RPCArg::Optional::NO,
                 "The block height to prune up to. May be set to a discrete "
                 "height, or a unix timestamp\n"
                 "                  to prune blocks whose block time is at "
                 "least 2 hours older than the provided timestamp."},
            },
            RPCResult{"n    (numeric) Height of the last block pruned.\n"},
            RPCExamples{HelpExampleCli("pruneblockchain", "1000") +
                        HelpExampleRpc("pruneblockchain", "1000")},
        }
                                     .ToString());
    }

    if (!fPruneMode) {
        throw JSONRPCError(
            RPC_MISC_ERROR,
            "Cannot prune blocks because node is not in prune mode.");
    }

    LOCK(cs_main);

    int heightParam = request.params[0].get_int();
    if (heightParam < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative block height.");
    }

    // Height value more than a billion is too high to be a block height, and
    // too low to be a block time (corresponds to timestamp from Sep 2001).
    if (heightParam > 1000000000) {
        // Add a 2 hour buffer to include blocks which might have had old
        // timestamps
        CBlockIndex *pindex =
            ::ChainActive().FindEarliestAtLeast(heightParam - TIMESTAMP_WINDOW);
        if (!pindex) {
            throw JSONRPCError(
                RPC_INVALID_PARAMETER,
                "Could not find block with at least the specified timestamp.");
        }
        heightParam = pindex->nHeight;
    }

    unsigned int height = (unsigned int)heightParam;
    unsigned int chainHeight = (unsigned int)::ChainActive().Height();
    if (chainHeight < config.GetChainParams().PruneAfterHeight()) {
        throw JSONRPCError(RPC_MISC_ERROR,
                           "Blockchain is too short for pruning.");
    } else if (height > chainHeight) {
        throw JSONRPCError(
            RPC_INVALID_PARAMETER,
            "Blockchain is shorter than the attempted prune height.");
    } else if (height > chainHeight - MIN_BLOCKS_TO_KEEP) {
        LogPrint(BCLog::RPC, "Attempt to prune blocks close to the tip. "
                             "Retaining the minimum number of blocks.\n");
        height = chainHeight - MIN_BLOCKS_TO_KEEP;
    }

    PruneBlockFilesManual(height);
    return uint64_t(height);
}

static UniValue gettxoutsetinfo(const Config &config,
                                const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(RPCHelpMan{
            "gettxoutsetinfo",
            "\nReturns statistics about the unspent transaction output set.\n"
            "Note this call may take some time.\n",
            {},
            RPCResult{
                "{\n"
                "  \"height\":n,     (numeric) The current block height "
                "(index)\n"
                "  \"bestblock\": \"hex\",   (string) the best block hash hex\n"
                "  \"transactions\": n,      (numeric) The number of "
                "transactions\n"
                "  \"txouts\": n,            (numeric) The number of output "
                "transactions\n"
                "  \"bogosize\": n,          (numeric) A database-independent "
                "metric for UTXO set size\n"
                "  \"hash_serialized\": \"hash\",   (string) The serialized "
                "hash\n"
                "  \"disk_size\": n,         (numeric) The estimated size of "
                "the chainstate on disk\n"
                "  \"total_amount\": x.xxx          (numeric) The total "
                "amount\n"
                "}\n"},
            RPCExamples{HelpExampleCli("gettxoutsetinfo", "") +
                        HelpExampleRpc("gettxoutsetinfo", "")},
        }
                                     .ToString());
    }

    UniValue ret(UniValue::VOBJ);

    CCoinsStats stats;
    ::ChainstateActive().ForceFlushStateToDisk();
    if (GetUTXOStats(pcoinsdbview.get(), stats)) {
        ret.pushKV("height", int64_t(stats.nHeight));
        ret.pushKV("bestblock", stats.hashBlock.GetHex());
        ret.pushKV("transactions", int64_t(stats.nTransactions));
        ret.pushKV("txouts", int64_t(stats.nTransactionOutputs));
        ret.pushKV("bogosize", int64_t(stats.nBogoSize));
        ret.pushKV("hash_serialized", stats.hashSerialized.GetHex());
        ret.pushKV("disk_size", stats.nDiskSize);
        ret.pushKV("total_amount", ValueFromAmount(stats.nTotalAmount));
    } else {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Unable to read UTXO set");
    }
    return ret;
}

UniValue gettxout(const Config &config, const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() < 2 ||
        request.params.size() > 3) {
        throw std::runtime_error(RPCHelpMan{
            "gettxout",
            "\nReturns details about an unspent transaction output.\n",
            {
                {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO,
                 "The transaction id"},
                {"n", RPCArg::Type::NUM, RPCArg::Optional::NO, "vout number"},
                {"include_mempool", RPCArg::Type::BOOL, /* default */ "true",
                 "Whether to include the mempool. Note that an unspent "
                 "output that is spent in the mempool won't appear."},
            },
            RPCResult{
                "{\n"
                "  \"bestblock\" : \"hash\",    (string) the block hash\n"
                "  \"confirmations\" : n,       (numeric) The number of "
                "confirmations\n"
                "  \"value\" : x.xxx,           (numeric) The transaction "
                "value in " +
                CURRENCY_UNIT +
                "\n"
                "  \"scriptPubKey\" : {         (json object)\n"
                "     \"asm\" : \"code\",       (string) \n"
                "     \"hex\" : \"hex\",        (string) \n"
                "     \"reqSigs\" : n,          (numeric) Number of required "
                "signatures\n"
                "     \"type\" : \"pubkeyhash\", (string) The type, eg "
                "pubkeyhash\n"
                "     \"addresses\" : [          (array of string) array of "
                "bitcoin addresses\n"
                "        \"address\"     (string) bitcoin address\n"
                "        ,...\n"
                "     ]\n"
                "  },\n"
                "  \"coinbase\" : true|false   (boolean) Coinbase or not\n"
                "}\n"},
            RPCExamples{"\nGet unspent transactions\n" +
                        HelpExampleCli("listunspent", "") +
                        "\nView the details\n" +
                        HelpExampleCli("gettxout", "\"txid\" 1") +
                        "\nAs a JSON-RPC call\n" +
                        HelpExampleRpc("gettxout", "\"txid\", 1")},
        }
                                     .ToString());
    }

    LOCK(cs_main);

    UniValue ret(UniValue::VOBJ);

    TxId txid(ParseHashV(request.params[0], "txid"));
    int n = request.params[1].get_int();
    COutPoint out(txid, n);
    bool fMempool = true;
    if (!request.params[2].isNull()) {
        fMempool = request.params[2].get_bool();
    }

    Coin coin;
    if (fMempool) {
        LOCK(g_mempool.cs);
        CCoinsViewMemPool view(pcoinsTip.get(), g_mempool);
        if (!view.GetCoin(out, coin) || g_mempool.isSpent(out)) {
            return NullUniValue;
        }
    } else {
        if (!pcoinsTip->GetCoin(out, coin)) {
            return NullUniValue;
        }
    }

    const CBlockIndex *pindex = LookupBlockIndex(pcoinsTip->GetBestBlock());
    ret.pushKV("bestblock", pindex->GetBlockHash().GetHex());
    if (coin.GetHeight() == MEMPOOL_HEIGHT) {
        ret.pushKV("confirmations", 0);
    } else {
        ret.pushKV("confirmations",
                   int64_t(pindex->nHeight - coin.GetHeight() + 1));
    }
    ret.pushKV("value", ValueFromAmount(coin.GetTxOut().nValue));
    UniValue o(UniValue::VOBJ);
    ScriptPubKeyToUniv(coin.GetTxOut().scriptPubKey, o, true);
    ret.pushKV("scriptPubKey", o);
    ret.pushKV("coinbase", coin.IsCoinBase());

    return ret;
}

static UniValue verifychain(const Config &config,
                            const JSONRPCRequest &request) {
    int nCheckLevel = gArgs.GetArg("-checklevel", DEFAULT_CHECKLEVEL);
    int nCheckDepth = gArgs.GetArg("-checkblocks", DEFAULT_CHECKBLOCKS);
    if (request.fHelp || request.params.size() > 2) {
        throw std::runtime_error(RPCHelpMan{
            "verifychain",
            "\nVerifies blockchain database.\n",
            {
                {"checklevel", RPCArg::Type::NUM,
                 /* default */ strprintf("%d, range=0-4", nCheckLevel),
                 "How thorough the block verification is."},
                {"nblocks", RPCArg::Type::NUM,
                 /* default */ strprintf("%d, 0=all", nCheckDepth),
                 "The number of blocks to check."},
            },
            RPCResult{"true|false       (boolean) Verified or not\n"},
            RPCExamples{HelpExampleCli("verifychain", "") +
                        HelpExampleRpc("verifychain", "")},
        }
                                     .ToString());
    }

    LOCK(cs_main);

    if (!request.params[0].isNull()) {
        nCheckLevel = request.params[0].get_int();
    }
    if (!request.params[1].isNull()) {
        nCheckDepth = request.params[1].get_int();
    }

    return CVerifyDB().VerifyDB(config, pcoinsTip.get(), nCheckLevel,
                                nCheckDepth);
}

static void BIP9SoftForkDescPushBack(UniValue &softforks,
                                     const Consensus::Params &consensusParams,
                                     Consensus::DeploymentPos id)
    EXCLUSIVE_LOCKS_REQUIRED(cs_main) {
    // For BIP9 deployments.
    // Deployments (e.g. testdummy) with timeout value before Jan 1, 2009 are
    // hidden. A timeout value of 0 guarantees a softfork will never be
    // activated. This is used when merging logic to implement a proposed
    // softfork without a specified deployment schedule.
    if (consensusParams.vDeployments[id].nTimeout <= 1230768000) {
        return;
    }

    UniValue bip9(UniValue::VOBJ);
    const ThresholdState thresholdState =
        VersionBitsTipState(consensusParams, id);
    switch (thresholdState) {
        case ThresholdState::DEFINED:
            bip9.pushKV("status", "defined");
            break;
        case ThresholdState::STARTED:
            bip9.pushKV("status", "started");
            break;
        case ThresholdState::LOCKED_IN:
            bip9.pushKV("status", "locked_in");
            break;
        case ThresholdState::ACTIVE:
            bip9.pushKV("status", "active");
            break;
        case ThresholdState::FAILED:
            bip9.pushKV("status", "failed");
            break;
    }
    if (ThresholdState::STARTED == thresholdState) {
        bip9.pushKV("bit", consensusParams.vDeployments[id].bit);
    }
    bip9.pushKV("start_time", consensusParams.vDeployments[id].nStartTime);
    bip9.pushKV("timeout", consensusParams.vDeployments[id].nTimeout);
    int64_t since_height = VersionBitsTipStateSinceHeight(consensusParams, id);
    bip9.pushKV("since", since_height);
    if (ThresholdState::STARTED == thresholdState) {
        UniValue statsUV(UniValue::VOBJ);
        BIP9Stats statsStruct = VersionBitsTipStatistics(consensusParams, id);
        statsUV.pushKV("period", statsStruct.period);
        statsUV.pushKV("threshold", statsStruct.threshold);
        statsUV.pushKV("elapsed", statsStruct.elapsed);
        statsUV.pushKV("count", statsStruct.count);
        statsUV.pushKV("possible", statsStruct.possible);
        bip9.pushKV("statistics", statsUV);
    }

    UniValue rv(UniValue::VOBJ);
    rv.pushKV("type", "bip9");
    rv.pushKV("bip9", bip9);
    if (ThresholdState::ACTIVE == thresholdState) {
        rv.pushKV("height", since_height);
    }
    rv.pushKV("active", ThresholdState::ACTIVE == thresholdState);

    softforks.pushKV(VersionBitsDeploymentInfo[id].name, rv);
}

UniValue getblockchaininfo(const Config &config,
                           const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(RPCHelpMan{
            "getblockchaininfo",
            "Returns an object containing various state info "
            "regarding blockchain processing.\n",
            {},
            RPCResult{
                "{\n"
                "  \"chain\": \"xxxx\",              (string) current network "
                "name as defined in BIP70 (main, test, regtest)\n"
                "  \"blocks\": xxxxxx,             (numeric) the current "
                "number of blocks processed in the server\n"
                "  \"headers\": xxxxxx,            (numeric) the current "
                "number of headers we have validated\n"
                "  \"bestblockhash\": \"...\",       (string) the hash of the "
                "currently best block\n"
                "  \"difficulty\": xxxxxx,         (numeric) the current "
                "difficulty\n"
                "  \"mediantime\": xxxxxx,         (numeric) median time for "
                "the current best block\n"
                "  \"verificationprogress\": xxxx, (numeric) estimate of "
                "verification progress [0..1]\n"
                "  \"initialblockdownload\": xxxx, (bool) (debug information) "
                "estimate of whether this node is in Initial Block Download "
                "mode.\n"
                "  \"chainwork\": \"xxxx\"           (string) total amount of "
                "work in active chain, in hexadecimal\n"
                "  \"size_on_disk\": xxxxxx,       (numeric) the estimated "
                "size of the block and undo files on disk\n"
                "  \"pruned\": xx,                 (boolean) if the blocks are "
                "subject to pruning\n"
                "  \"pruneheight\": xxxxxx,        (numeric) lowest-height "
                "complete block stored (only present if pruning is enabled)\n"
                "  \"automatic_pruning\": xx,      (boolean) whether automatic "
                "pruning is enabled (only present if pruning is enabled)\n"
                "  \"prune_target_size\": xxxxxx,  (numeric) the target size "
                "used by pruning (only present if automatic pruning is "
                "enabled)\n"
                "  \"softforks\": {                (object) status of "
                "softforks in progress\n"
                "    \"xxxx\" : {                  (string) name of the "
                "softfork\n"
                "      \"type\" : \"bip9\",        (string) currently only set "
                "to \"bip9\"\n"
                "      \"bip9\" : {                (object) status of bip9 "
                "softforks (only for \"bip9\" type)\n"
                "        \"status\": \"xxxx\",     (string) one of "
                "\"defined\", \"started\", \"locked_in\", \"active\", "
                "\"failed\"\n"
                "        \"bit\": xx,              (numeric) the bit (0-28) in "
                "the block version field used to signal this softfork (only "
                "for \"started\" status)\n"
                "        \"startTime\": xx,        (numeric) the minimum "
                "median time past of a block at which the bit gains its "
                "meaning\n"
                "        \"timeout\": xx,          (numeric) the median time "
                "past of a block at which the deployment is considered failed "
                "if not "
                "yet locked in\n"
                "        \"since\": xx,            (numeric) height of the "
                "first block to which the status applies\n"
                "        \"statistics\": {         (object) numeric statistics "
                "about BIP9 signalling for a softfork (only for \"started\" "
                "status)\n"
                "          \"period\": xx,         (numeric) the length in "
                "blocks of the BIP9 signalling period \n"
                "          \"threshold\": xx,      (numeric) the number of "
                "blocks with the version bit set required to activate the "
                "feature \n"
                "          \"elapsed\": xx,        (numeric) the number of "
                "blocks elapsed since the beginning of the current period \n"
                "          \"count\": xx,          (numeric) the number of "
                "blocks with the version bit set in the current period \n"
                "          \"possible\": xx        (boolean) returns false if "
                "there are not enough blocks left in this period to pass "
                "activation threshold\n"
                "        },\n"
                "        \"active\": xx,           (boolean) true if the rules "
                "are enforced for the mempool and the next block\n"
                "      }\n"
                "    }\n"
                "  }\n"
                "  \"warnings\" : \"...\",           (string) any network and "
                "blockchain warnings.\n"
                "}\n"},
            RPCExamples{HelpExampleCli("getblockchaininfo", "") +
                        HelpExampleRpc("getblockchaininfo", "")},
        }
                                     .ToString());
    }

    LOCK(cs_main);

    const CChainParams &chainparams = config.GetChainParams();

    const CBlockIndex *tip = ::ChainActive().Tip();
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("chain", chainparams.NetworkIDString());
    obj.pushKV("blocks", int(::ChainActive().Height()));
    obj.pushKV("headers", pindexBestHeader ? pindexBestHeader->nHeight : -1);
    obj.pushKV("bestblockhash", tip->GetBlockHash().GetHex());
    obj.pushKV("difficulty", double(GetDifficulty(tip)));
    obj.pushKV("mediantime", int64_t(tip->GetMedianTimePast()));
    obj.pushKV("verificationprogress",
               GuessVerificationProgress(Params().TxData(), tip));
    obj.pushKV("initialblockdownload",
               ::ChainstateActive().IsInitialBlockDownload());
    obj.pushKV("chainwork", tip->nChainWork.GetHex());
    obj.pushKV("size_on_disk", CalculateCurrentUsage());
    obj.pushKV("pruned", fPruneMode);

    if (fPruneMode) {
        const CBlockIndex *block = tip;
        assert(block);
        while (block->pprev && (block->pprev->nStatus.hasData())) {
            block = block->pprev;
        }

        obj.pushKV("pruneheight", block->nHeight);

        // if 0, execution bypasses the whole if block.
        bool automatic_pruning = (gArgs.GetArg("-prune", 0) != 1);
        obj.pushKV("automatic_pruning", automatic_pruning);
        if (automatic_pruning) {
            obj.pushKV("prune_target_size", nPruneTarget);
        }
    }

    UniValue softforks(UniValue::VOBJ);
    for (int i = 0; i < (int)Consensus::MAX_VERSION_BITS_DEPLOYMENTS; i++) {
        BIP9SoftForkDescPushBack(softforks, chainparams.GetConsensus(),
                                 Consensus::DeploymentPos(i));
    }
    obj.pushKV("softforks", softforks);

    obj.pushKV("warnings", GetWarnings("statusbar"));
    return obj;
}

/** Comparison function for sorting the getchaintips heads.  */
struct CompareBlocksByHeight {
    bool operator()(const CBlockIndex *a, const CBlockIndex *b) const {
        // Make sure that unequal blocks with the same height do not compare
        // equal. Use the pointers themselves to make a distinction.
        if (a->nHeight != b->nHeight) {
            return (a->nHeight > b->nHeight);
        }

        return a < b;
    }
};

static UniValue getchaintips(const Config &config,
                             const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(RPCHelpMan{
            "getchaintips",
            "Return information about all known tips in the block tree, "
            "including the main chain as well as orphaned branches.\n",
            {},
            RPCResult{
                "[\n"
                "  {\n"
                "    \"height\": xxxx,         (numeric) height of the chain "
                "tip\n"
                "    \"hash\": \"xxxx\",         (string) block hash of the "
                "tip\n"
                "    \"branchlen\": 0          (numeric) zero for main chain\n"
                "    \"status\": \"active\"      (string) \"active\" for the "
                "main chain\n"
                "  },\n"
                "  {\n"
                "    \"height\": xxxx,\n"
                "    \"hash\": \"xxxx\",\n"
                "    \"branchlen\": 1          (numeric) length of branch "
                "connecting the tip to the main chain\n"
                "    \"status\": \"xxxx\"        (string) status of the chain "
                "(active, valid-fork, valid-headers, headers-only, invalid)\n"
                "  }\n"
                "]\n"
                "Possible values for status:\n"
                "1.  \"invalid\"               This branch contains at least "
                "one invalid block\n"
                "2.  \"parked\"                This branch contains at least "
                "one parked block\n"
                "3.  \"headers-only\"          Not all blocks for this branch "
                "are available, but the headers are valid\n"
                "4.  \"valid-headers\"         All blocks are available for "
                "this branch, but they were never fully validated\n"
                "5.  \"valid-fork\"            This branch is not part of the "
                "active chain, but is fully validated\n"
                "6.  \"active\"                This is the tip of the active "
                "main chain, which is certainly valid\n"},
            RPCExamples{HelpExampleCli("getchaintips", "") +
                        HelpExampleRpc("getchaintips", "")},
        }
                                     .ToString());
    }

    LOCK(cs_main);

    /**
     * Idea:  the set of chain tips is ::ChainActive().tip, plus orphan blocks
     * which do not have another orphan building off of them. Algorithm:
     *  - Make one pass through mapBlockIndex, picking out the orphan blocks,
     * and also storing a set of the orphan block's pprev pointers.
     *  - Iterate through the orphan blocks. If the block isn't pointed to by
     * another orphan, it is a chain tip.
     *  - add ::ChainActive().Tip()
     */
    std::set<const CBlockIndex *, CompareBlocksByHeight> setTips;
    std::set<const CBlockIndex *> setOrphans;
    std::set<const CBlockIndex *> setPrevs;

    for (const std::pair<const BlockHash, CBlockIndex *> &item :
         mapBlockIndex) {
        if (!::ChainActive().Contains(item.second)) {
            setOrphans.insert(item.second);
            setPrevs.insert(item.second->pprev);
        }
    }

    for (std::set<const CBlockIndex *>::iterator it = setOrphans.begin();
         it != setOrphans.end(); ++it) {
        if (setPrevs.erase(*it) == 0) {
            setTips.insert(*it);
        }
    }

    // Always report the currently active tip.
    setTips.insert(::ChainActive().Tip());

    /* Construct the output array.  */
    UniValue res(UniValue::VARR);
    for (const CBlockIndex *block : setTips) {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("height", block->nHeight);
        obj.pushKV("hash", block->phashBlock->GetHex());

        const int branchLen =
            block->nHeight - ::ChainActive().FindFork(block)->nHeight;
        obj.pushKV("branchlen", branchLen);

        std::string status;
        if (::ChainActive().Contains(block)) {
            // This block is part of the currently active chain.
            status = "active";
        } else if (block->nStatus.isInvalid()) {
            // This block or one of its ancestors is invalid.
            status = "invalid";
        } else if (block->nStatus.isOnParkedChain()) {
            // This block or one of its ancestors is parked.
            status = "parked";
        } else if (!block->HaveTxsDownloaded()) {
            // This block cannot be connected because full block data for it or
            // one of its parents is missing.
            status = "headers-only";
        } else if (block->IsValid(BlockValidity::SCRIPTS)) {
            // This block is fully validated, but no longer part of the active
            // chain. It was probably the active block once, but was
            // reorganized.
            status = "valid-fork";
        } else if (block->IsValid(BlockValidity::TREE)) {
            // The headers for this block are valid, but it has not been
            // validated. It was probably never part of the most-work chain.
            status = "valid-headers";
        } else {
            // No clue.
            status = "unknown";
        }
        obj.pushKV("status", status);

        res.push_back(obj);
    }

    return res;
}

UniValue MempoolInfoToJSON(const CTxMemPool &pool) {
    UniValue ret(UniValue::VOBJ);
    ret.pushKV("loaded", pool.IsLoaded());
    ret.pushKV("size", (int64_t)pool.size());
    ret.pushKV("bytes", (int64_t)pool.GetTotalTxSize());
    ret.pushKV("usage", (int64_t)pool.DynamicMemoryUsage());
    size_t maxmempool =
        gArgs.GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000;
    ret.pushKV("maxmempool", (int64_t)maxmempool);
    ret.pushKV(
        "mempoolminfee",
        ValueFromAmount(std::max(pool.GetMinFee(maxmempool), ::minRelayTxFee)
                            .GetFeePerK()));
    ret.pushKV("minrelaytxfee", ValueFromAmount(::minRelayTxFee.GetFeePerK()));

    return ret;
}

static UniValue getmempoolinfo(const Config &config,
                               const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(RPCHelpMan{
            "getmempoolinfo",
            "\nReturns details on the active state of the TX memory "
            "pool.\n",
            {},
            RPCResult{
                "{\n"
                "  \"loaded\": true|false         (boolean) True if the "
                "mempool is fully loaded\n"
                "  \"size\": xxxxx,               (numeric) Current tx count\n"
                "  \"bytes\": xxxxx,              (numeric) Transaction size.\n"
                "  \"usage\": xxxxx,              (numeric) Total memory usage "
                "for the mempool\n"
                "  \"maxmempool\": xxxxx,         (numeric) Maximum memory "
                "usage for the mempool\n"
                "  \"mempoolminfee\": xxxxx       (numeric) Minimum fee rate "
                "in " +
                CURRENCY_UNIT +
                "/kB for tx to be accepted. Is the maximum of minrelaytxfee "
                "and minimum mempool fee\n"
                "  \"minrelaytxfee\": xxxxx       (numeric) Current minimum "
                "relay fee for transactions\n"
                "}\n"},
            RPCExamples{HelpExampleCli("getmempoolinfo", "") +
                        HelpExampleRpc("getmempoolinfo", "")},
        }
                                     .ToString());
    }

    return MempoolInfoToJSON(::g_mempool);
}

static UniValue preciousblock(const Config &config,
                              const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(RPCHelpMan{
            "preciousblock",
            "\nTreats a block as if it were received before others "
            "with the same work.\n"
            "\nA later preciousblock call can override the effect "
            "of an earlier one.\n"
            "\nThe effects of preciousblock are not retained across "
            "restarts.\n",
            {
                {"blockhash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO,
                 "the hash of the block to mark as precious"},
            },
            RPCResults{},
            RPCExamples{HelpExampleCli("preciousblock", "\"blockhash\"") +
                        HelpExampleRpc("preciousblock", "\"blockhash\"")},
        }
                                     .ToString());
    }

    BlockHash hash(ParseHashV(request.params[0], "blockhash"));
    CBlockIndex *pblockindex;

    {
        LOCK(cs_main);
        pblockindex = LookupBlockIndex(hash);
        if (!pblockindex) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        }
    }

    CValidationState state;
    PreciousBlock(config, state, pblockindex);

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    return NullUniValue;
}

UniValue finalizeblock(const Config &config, const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(RPCHelpMan{
            "finalizeblock",
            "\nTreats a block as final. It cannot be reorged. Any chain\n"
            "that does not contain this block is invalid. Used on a less\n"
            "work chain, it can effectively PUTS YOU OUT OF CONSENSUS.\n"
            "USE WITH CAUTION!\n",
            {
                {"blockhash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO,
                 "the hash of the block to mark as invalid"},
            },
            RPCResults{},
            RPCExamples{HelpExampleCli("invalidateblock", "\"blockhash\"") +
                        HelpExampleRpc("invalidateblock", "\"blockhash\"")},
        }
                                     .ToString());
    }

    std::string strHash = request.params[0].get_str();
    BlockHash hash(uint256S(strHash));
    CValidationState state;

    {
        LOCK(cs_main);
        CBlockIndex *pblockindex = LookupBlockIndex(hash);
        if (!pblockindex) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        }

        FinalizeBlockAndInvalidate(config, state, pblockindex);
    }

    if (state.IsValid()) {
        ActivateBestChain(config, state);
    }

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, FormatStateMessage(state));
    }

    return NullUniValue;
}

static UniValue invalidateblock(const Config &config,
                                const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(RPCHelpMan{
            "invalidateblock",
            "\nPermanently marks a block as invalid, as if it "
            "violated a consensus rule.\n",
            {
                {"blockhash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO,
                 "the hash of the block to mark as invalid"},
            },
            RPCResults{},
            RPCExamples{HelpExampleCli("invalidateblock", "\"blockhash\"") +
                        HelpExampleRpc("invalidateblock", "\"blockhash\"")},
        }
                                     .ToString());
    }

    const BlockHash hash(ParseHashV(request.params[0], "blockhash"));
    CValidationState state;

    CBlockIndex *pblockindex;
    {
        LOCK(cs_main);
        pblockindex = LookupBlockIndex(hash);
        if (!pblockindex) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        }
    }
    InvalidateBlock(config, state, pblockindex);

    if (state.IsValid()) {
        ActivateBestChain(config, state);
    }

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, FormatStateMessage(state));
    }

    return NullUniValue;
}

UniValue parkblock(const Config &config, const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(RPCHelpMan{
            "parkblock",
            "\nMarks a block as parked.\n",
            {
                {"blockhash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO,
                 "the hash of the block to park"},
            },
            RPCResults{},
            RPCExamples{HelpExampleCli("parkblock", "\"blockhash\"") +
                        HelpExampleRpc("parkblock", "\"blockhash\"")},
        }
                                     .ToString());
    }

    const std::string strHash = request.params[0].get_str();
    const BlockHash hash(uint256S(strHash));
    CValidationState state;

    CBlockIndex *pblockindex;
    {
        LOCK(cs_main);
        if (mapBlockIndex.count(hash) == 0) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        }

        pblockindex = mapBlockIndex[hash];
    }
    ParkBlock(config, state, pblockindex);

    if (state.IsValid()) {
        ActivateBestChain(config, state);
    }

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    return NullUniValue;
}

static UniValue reconsiderblock(const Config &config,
                                const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(RPCHelpMan{
            "reconsiderblock",
            "\nRemoves invalidity status of a block and its descendants, "
            "reconsider them for activation.\n"
            "This can be used to undo the effects of invalidateblock.\n",
            {
                {"blockhash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO,
                 "the hash of the block to reconsider"},
            },
            RPCResults{},
            RPCExamples{HelpExampleCli("reconsiderblock", "\"blockhash\"") +
                        HelpExampleRpc("reconsiderblock", "\"blockhash\"")},
        }
                                     .ToString());
    }

    const BlockHash hash(ParseHashV(request.params[0], "blockhash"));

    {
        LOCK(cs_main);
        CBlockIndex *pblockindex = LookupBlockIndex(hash);
        if (!pblockindex) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        }

        ResetBlockFailureFlags(pblockindex);
    }

    CValidationState state;
    ActivateBestChain(config, state);

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, FormatStateMessage(state));
    }

    return NullUniValue;
}

UniValue unparkblock(const Config &config, const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(RPCHelpMan{
            "unparkblock",
            "\nRemoves parked status of a block and its descendants, "
            "reconsider them for activation.\n"
            "This can be used to undo the effects of parkblock.\n",
            {
                {"blockhash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO,
                 "the hash of the block to unpark"},
            },
            RPCResults{},
            RPCExamples{HelpExampleCli("unparkblock", "\"blockhash\"") +
                        HelpExampleRpc("unparkblock", "\"blockhash\"")},
        }
                                     .ToString());
    }

    const std::string strHash = request.params[0].get_str();
    const BlockHash hash(uint256S(strHash));

    {
        LOCK(cs_main);
        if (mapBlockIndex.count(hash) == 0) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        }

        CBlockIndex *pblockindex = mapBlockIndex[hash];
        UnparkBlockAndChildren(pblockindex);
    }

    CValidationState state;
    ActivateBestChain(config, state);

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    return NullUniValue;
}

static UniValue getchaintxstats(const Config &config,
                                const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() > 2) {
        throw std::runtime_error(RPCHelpMan{
            "getchaintxstats",
            "\nCompute statistics about the total number and rate "
            "of transactions in the chain.\n",
            {
                {"nblocks", RPCArg::Type::NUM, /* default */ "one month",
                 "Size of the window in number of blocks"},
                {"blockhash", RPCArg::Type::STR_HEX, /* default */ "chain tip",
                 "The hash of the block that ends the window."},
            },
            RPCResult{
                "{\n"
                "  \"time\": xxxxx,                         (numeric) The "
                "timestamp for the final block in the window in UNIX format.\n"
                "  \"txcount\": xxxxx,                      (numeric) The "
                "total number of transactions in the chain up to that point.\n"
                "  \"window_final_block_hash\": \"...\",      (string) The "
                "hash of the final block in the window.\n"
                "  \"window_block_count\": xxxxx,           (numeric) Size of "
                "the window in number of blocks.\n"
                "  \"window_tx_count\": xxxxx,              (numeric) The "
                "number of transactions in the window. Only returned if "
                "\"window_block_count\" is > 0.\n"
                "  \"window_interval\": xxxxx,              (numeric) The "
                "elapsed time in the window in seconds. Only returned if "
                "\"window_block_count\" is > 0.\n"
                "  \"txrate\": x.xx,                        (numeric) The "
                "average rate of transactions per second in the window. Only "
                "returned if \"window_interval\" is > 0.\n"
                "}\n"},
            RPCExamples{HelpExampleCli("getchaintxstats", "") +
                        HelpExampleRpc("getchaintxstats", "2016")},
        }
                                     .ToString());
    }

    const CBlockIndex *pindex;

    // By default: 1 month
    int blockcount = 30 * 24 * 60 * 60 /
                     config.GetChainParams().GetConsensus().nPowTargetSpacing;

    if (request.params[1].isNull()) {
        LOCK(cs_main);
        pindex = ::ChainActive().Tip();
    } else {
        BlockHash hash(ParseHashV(request.params[1], "blockhash"));
        LOCK(cs_main);
        pindex = LookupBlockIndex(hash);
        if (!pindex) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        }
        if (!::ChainActive().Contains(pindex)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "Block is not in main chain");
        }
    }

    assert(pindex != nullptr);

    if (request.params[0].isNull()) {
        blockcount = std::max(0, std::min(blockcount, pindex->nHeight - 1));
    } else {
        blockcount = request.params[0].get_int();

        if (blockcount < 0 ||
            (blockcount > 0 && blockcount >= pindex->nHeight)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid block count: "
                                                      "should be between 0 and "
                                                      "the block's height - 1");
        }
    }

    const CBlockIndex *pindexPast =
        pindex->GetAncestor(pindex->nHeight - blockcount);
    int nTimeDiff =
        pindex->GetMedianTimePast() - pindexPast->GetMedianTimePast();
    int nTxDiff = pindex->nChainTx - pindexPast->nChainTx;

    UniValue ret(UniValue::VOBJ);
    ret.pushKV("time", int64_t(pindex->nTime));
    ret.pushKV("txcount", int64_t(pindex->nChainTx));
    ret.pushKV("window_final_block_hash", pindex->GetBlockHash().GetHex());
    ret.pushKV("window_block_count", blockcount);
    if (blockcount > 0) {
        ret.pushKV("window_tx_count", nTxDiff);
        ret.pushKV("window_interval", nTimeDiff);
        if (nTimeDiff > 0) {
            ret.pushKV("txrate", double(nTxDiff) / nTimeDiff);
        }
    }

    return ret;
}

template <typename T>
static T CalculateTruncatedMedian(std::vector<T> &scores) {
    size_t size = scores.size();
    if (size == 0) {
        return T();
    }

    std::sort(scores.begin(), scores.end());
    if (size % 2 == 0) {
        return (scores[size / 2 - 1] + scores[size / 2]) / 2;
    } else {
        return scores[size / 2];
    }
}

template <typename T> static inline bool SetHasKeys(const std::set<T> &set) {
    return false;
}
template <typename T, typename Tk, typename... Args>
static inline bool SetHasKeys(const std::set<T> &set, const Tk &key,
                              const Args &... args) {
    return (set.count(key) != 0) || SetHasKeys(set, args...);
}

// outpoint (needed for the utxo index) + nHeight + fCoinBase
static constexpr size_t PER_UTXO_OVERHEAD =
    sizeof(COutPoint) + sizeof(uint32_t) + sizeof(bool);

static UniValue getblockstats(const Config &config,
                              const JSONRPCRequest &request) {
    const RPCHelpMan help{
        "getblockstats",
        "\nCompute per block statistics for a given window. All amounts are "
        "in " +
            CURRENCY_UNIT +
            ".\n"
            "It won't work for some heights with pruning.\n"
            "It won't work without -txindex for utxo_size_inc, *fee or "
            "*feerate stats.\n",
        {
            {"hash_or_height",
             RPCArg::Type::NUM,
             RPCArg::Optional::NO,
             "The block hash or height of the target block",
             "",
             {"", "string or numeric"}},
            {"stats",
             RPCArg::Type::ARR,
             /* default */ "all values",
             "Values to plot (see result below)",
             {
                 {"height", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                  "Selected statistic"},
                 {"time", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                  "Selected statistic"},
             },
             "stats"},
        },
        RPCResult{
            "{                           (json object)\n"
            "  \"avgfee\": x.xxx,          (numeric) Average fee in the block\n"
            "  \"avgfeerate\": x.xxx,      (numeric) Average feerate (in " +
            CURRENCY_UNIT +
            " per byte)\n"
            "  \"avgtxsize\": xxxxx,       (numeric) Average transaction size\n"
            "  \"blockhash\": xxxxx,       (string) The block hash (to check "
            "for potential reorgs)\n"
            "  \"height\": xxxxx,          (numeric) The height of the block\n"
            "  \"ins\": xxxxx,             (numeric) The number of inputs "
            "(excluding coinbase)\n"
            "  \"maxfee\": xxxxx,          (numeric) Maximum fee in the block\n"
            "  \"maxfeerate\": xxxxx,      (numeric) Maximum feerate (in " +
            CURRENCY_UNIT +
            " per byte)\n"
            "  \"maxtxsize\": xxxxx,       (numeric) Maximum transaction size\n"
            "  \"medianfee\": x.xxx,       (numeric) Truncated median fee in "
            "the block\n"
            "  \"medianfeerate\": x.xxx,   (numeric) Truncated median feerate "
            "(in " +
            CURRENCY_UNIT +
            " per byte)\n"
            "  \"mediantime\": xxxxx,      (numeric) The block median time "
            "past\n"
            "  \"mediantxsize\": xxxxx,    (numeric) Truncated median "
            "transaction size\n"
            "  \"minfee\": x.xxx,          (numeric) Minimum fee in the block\n"
            "  \"minfeerate\": xx.xx,      (numeric) Minimum feerate (in " +
            CURRENCY_UNIT +
            " per byte)\n"
            "  \"mintxsize\": xxxxx,       (numeric) Minimum transaction size\n"
            "  \"outs\": xxxxx,            (numeric) The number of outputs\n"
            "  \"subsidy\": x.xxx,         (numeric) The block subsidy\n"
            "  \"time\": xxxxx,            (numeric) The block time\n"
            "  \"total_out\": x.xxx,       (numeric) Total amount in all "
            "outputs (excluding coinbase and thus reward [ie subsidy + "
            "totalfee])\n"
            "  \"total_size\": xxxxx,      (numeric) Total size of all "
            "non-coinbase transactions\n"
            "  \"totalfee\": x.xxx,        (numeric) The fee total\n"
            "  \"txs\": xxxxx,             (numeric) The number of "
            "transactions (excluding coinbase)\n"
            "  \"utxo_increase\": xxxxx,   (numeric) The increase/decrease in "
            "the number of unspent outputs\n"
            "  \"utxo_size_inc\": xxxxx,   (numeric) The increase/decrease in "
            "size for the utxo index (not discounting op_return and similar)\n"
            "}\n"},
        RPCExamples{HelpExampleCli("getblockstats",
                                   "1000 '[\"minfeerate\",\"avgfeerate\"]'") +
                    HelpExampleRpc("getblockstats",
                                   "1000 '[\"minfeerate\",\"avgfeerate\"]'")},
    };

    if (request.fHelp || !help.IsValidNumArgs(request.params.size())) {
        throw std::runtime_error(help.ToString());
    }

    LOCK(cs_main);

    CBlockIndex *pindex;
    if (request.params[0].isNum()) {
        const int height = request.params[0].get_int();
        const int current_tip = ::ChainActive().Height();
        if (height < 0) {
            throw JSONRPCError(
                RPC_INVALID_PARAMETER,
                strprintf("Target block height %d is negative", height));
        }
        if (height > current_tip) {
            throw JSONRPCError(
                RPC_INVALID_PARAMETER,
                strprintf("Target block height %d after current tip %d", height,
                          current_tip));
        }

        pindex = ::ChainActive()[height];
    } else {
        const BlockHash hash(ParseHashV(request.params[0], "hash_or_height"));
        pindex = LookupBlockIndex(hash);
        if (!pindex) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        }
        if (!::ChainActive().Contains(pindex)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               strprintf("Block is not in chain %s",
                                         Params().NetworkIDString()));
        }
    }

    assert(pindex != nullptr);

    std::set<std::string> stats;
    if (!request.params[1].isNull()) {
        const UniValue stats_univalue = request.params[1].get_array();
        for (unsigned int i = 0; i < stats_univalue.size(); i++) {
            const std::string stat = stats_univalue[i].get_str();
            stats.insert(stat);
        }
    }

    const CBlock block = GetBlockChecked(config, pindex);

    // Calculate everything if nothing selected (default)
    const bool do_all = stats.size() == 0;
    const bool do_mediantxsize = do_all || stats.count("mediantxsize") != 0;
    const bool do_medianfee = do_all || stats.count("medianfee") != 0;
    const bool do_medianfeerate = do_all || stats.count("medianfeerate") != 0;
    const bool loop_inputs =
        do_all || do_medianfee || do_medianfeerate ||
        SetHasKeys(stats, "utxo_size_inc", "totalfee", "avgfee", "avgfeerate",
                   "minfee", "maxfee", "minfeerate", "maxfeerate");
    const bool loop_outputs = do_all || loop_inputs || stats.count("total_out");
    const bool do_calculate_size =
        do_mediantxsize || loop_inputs ||
        SetHasKeys(stats, "total_size", "avgtxsize", "mintxsize", "maxtxsize");

    const int64_t blockMaxSize = config.GetMaxBlockSize();
    Amount maxfee = Amount::zero();
    Amount maxfeerate = Amount::zero();
    Amount minfee = MAX_MONEY;
    Amount minfeerate = MAX_MONEY;
    Amount total_out = Amount::zero();
    Amount totalfee = Amount::zero();
    int64_t inputs = 0;
    int64_t maxtxsize = 0;
    int64_t mintxsize = blockMaxSize;
    int64_t outputs = 0;
    int64_t total_size = 0;
    int64_t utxo_size_inc = 0;
    std::vector<Amount> fee_array;
    std::vector<Amount> feerate_array;
    std::vector<int64_t> txsize_array;

    const Consensus::Params &params = config.GetChainParams().GetConsensus();

    for (const auto &tx : block.vtx) {
        outputs += tx->vout.size();
        Amount tx_total_out = Amount::zero();
        if (loop_outputs) {
            for (const CTxOut &out : tx->vout) {
                tx_total_out += out.nValue;
                utxo_size_inc +=
                    GetSerializeSize(out, PROTOCOL_VERSION) + PER_UTXO_OVERHEAD;
            }
        }

        if (tx->IsCoinBase()) {
            continue;
        }

        // Don't count coinbase's fake input
        inputs += tx->vin.size();
        // Don't count coinbase reward
        total_out += tx_total_out;

        int64_t tx_size = 0;
        if (do_calculate_size) {
            tx_size = tx->GetTotalSize();
            if (do_mediantxsize) {
                txsize_array.push_back(tx_size);
            }
            maxtxsize = std::max(maxtxsize, tx_size);
            mintxsize = std::min(mintxsize, tx_size);
            total_size += tx_size;
        }

        if (loop_inputs) {
            if (!g_txindex) {
                throw JSONRPCError(RPC_INVALID_PARAMETER,
                                   "One or more of the selected stats requires "
                                   "-txindex enabled");
            }

            Amount tx_total_in = Amount::zero();
            for (const CTxIn &in : tx->vin) {
                CTransactionRef tx_in;
                BlockHash hashBlock;
                if (!GetTransaction(in.prevout.GetTxId(), tx_in, params,
                                    hashBlock)) {
                    throw JSONRPCError(RPC_INTERNAL_ERROR,
                                       std::string("Unexpected internal error "
                                                   "(tx index seems corrupt)"));
                }

                CTxOut prevoutput = tx_in->vout[in.prevout.GetN()];

                tx_total_in += prevoutput.nValue;
                utxo_size_inc -=
                    GetSerializeSize(prevoutput, PROTOCOL_VERSION) +
                    PER_UTXO_OVERHEAD;
            }

            Amount txfee = tx_total_in - tx_total_out;
            assert(MoneyRange(txfee));
            if (do_medianfee) {
                fee_array.push_back(txfee);
            }
            maxfee = std::max(maxfee, txfee);
            minfee = std::min(minfee, txfee);
            totalfee += txfee;

            Amount feerate = txfee / tx_size;
            if (do_medianfeerate) {
                feerate_array.push_back(feerate);
            }
            maxfeerate = std::max(maxfeerate, feerate);
            minfeerate = std::min(minfeerate, feerate);
        }
    }

    UniValue ret_all(UniValue::VOBJ);
    ret_all.pushKV("avgfee",
                   ValueFromAmount((block.vtx.size() > 1)
                                       ? totalfee / int((block.vtx.size() - 1))
                                       : Amount::zero()));
    ret_all.pushKV("avgfeerate",
                   ValueFromAmount((total_size > 0) ? totalfee / total_size
                                                    : Amount::zero()));
    ret_all.pushKV("avgtxsize", (block.vtx.size() > 1)
                                    ? total_size / (block.vtx.size() - 1)
                                    : 0);
    ret_all.pushKV("blockhash", pindex->GetBlockHash().GetHex());
    ret_all.pushKV("height", (int64_t)pindex->nHeight);
    ret_all.pushKV("ins", inputs);
    ret_all.pushKV("maxfee", ValueFromAmount(maxfee));
    ret_all.pushKV("maxfeerate", ValueFromAmount(maxfeerate));
    ret_all.pushKV("maxtxsize", maxtxsize);
    ret_all.pushKV("medianfee",
                   ValueFromAmount(CalculateTruncatedMedian(fee_array)));
    ret_all.pushKV("medianfeerate",
                   ValueFromAmount(CalculateTruncatedMedian(feerate_array)));
    ret_all.pushKV("mediantime", pindex->GetMedianTimePast());
    ret_all.pushKV("mediantxsize", CalculateTruncatedMedian(txsize_array));
    ret_all.pushKV(
        "minfee",
        ValueFromAmount((minfee == MAX_MONEY) ? Amount::zero() : minfee));
    ret_all.pushKV("minfeerate",
                   ValueFromAmount((minfeerate == MAX_MONEY) ? Amount::zero()
                                                             : minfeerate));
    ret_all.pushKV("mintxsize", mintxsize == blockMaxSize ? 0 : mintxsize);
    ret_all.pushKV("outs", outputs);
    ret_all.pushKV("subsidy", ValueFromAmount(GetBlockSubsidy(
                                  pindex->nHeight, Params().GetConsensus())));
    ret_all.pushKV("time", pindex->GetBlockTime());
    ret_all.pushKV("total_out", ValueFromAmount(total_out));
    ret_all.pushKV("total_size", total_size);
    ret_all.pushKV("totalfee", ValueFromAmount(totalfee));
    ret_all.pushKV("txs", (int64_t)block.vtx.size());
    ret_all.pushKV("utxo_increase", outputs - inputs);
    ret_all.pushKV("utxo_size_inc", utxo_size_inc);

    if (do_all) {
        return ret_all;
    }

    UniValue ret(UniValue::VOBJ);
    for (const std::string &stat : stats) {
        const UniValue &value = ret_all[stat];
        if (value.isNull()) {
            throw JSONRPCError(
                RPC_INVALID_PARAMETER,
                strprintf("Invalid selected statistic %s", stat));
        }
        ret.pushKV(stat, value);
    }
    return ret;
}

static UniValue savemempool(const Config &config,
                            const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(RPCHelpMan{
            "savemempool",
            "\nDumps the mempool to disk. It will fail until the "
            "previous dump is fully loaded.\n",
            {},
            RPCResults{},
            RPCExamples{HelpExampleCli("savemempool", "") +
                        HelpExampleRpc("savemempool", "")},
        }
                                     .ToString());
    }

    if (!::g_mempool.IsLoaded()) {
        throw JSONRPCError(RPC_MISC_ERROR, "The mempool was not loaded yet");
    }

    if (!DumpMempool(::g_mempool)) {
        throw JSONRPCError(RPC_MISC_ERROR, "Unable to dump mempool to disk");
    }

    return NullUniValue;
}

//! Search for a given set of pubkey scripts
static bool FindScriptPubKey(std::atomic<int> &scan_progress,
                             const std::atomic<bool> &should_abort,
                             int64_t &count, CCoinsViewCursor *cursor,
                             const std::set<CScript> &needles,
                             std::map<COutPoint, Coin> &out_results) {
    scan_progress = 0;
    count = 0;
    while (cursor->Valid()) {
        COutPoint key;
        Coin coin;
        if (!cursor->GetKey(key) || !cursor->GetValue(coin)) {
            return false;
        }
        if (++count % 8192 == 0) {
            boost::this_thread::interruption_point();
            if (should_abort) {
                // allow to abort the scan via the abort reference
                return false;
            }
        }
        if (count % 256 == 0) {
            // update progress reference every 256 item
            const TxId &txid = key.GetTxId();
            uint32_t high = 0x100 * *txid.begin() + *(txid.begin() + 1);
            scan_progress = int(high * 100.0 / 65536.0 + 0.5);
        }
        if (needles.count(coin.GetTxOut().scriptPubKey)) {
            out_results.emplace(key, coin);
        }
        cursor->Next();
    }
    scan_progress = 100;
    return true;
}

/** RAII object to prevent concurrency issue when scanning the txout set */
static std::mutex g_utxosetscan;
static std::atomic<int> g_scan_progress;
static std::atomic<bool> g_scan_in_progress;
static std::atomic<bool> g_should_abort_scan;
class CoinsViewScanReserver {
private:
    bool m_could_reserve;

public:
    explicit CoinsViewScanReserver() : m_could_reserve(false) {}

    bool reserve() {
        assert(!m_could_reserve);
        std::lock_guard<std::mutex> lock(g_utxosetscan);
        if (g_scan_in_progress) {
            return false;
        }
        g_scan_in_progress = true;
        m_could_reserve = true;
        return true;
    }

    ~CoinsViewScanReserver() {
        if (m_could_reserve) {
            std::lock_guard<std::mutex> lock(g_utxosetscan);
            g_scan_in_progress = false;
        }
    }
};

static UniValue scantxoutset(const Config &config,
                             const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() < 1 ||
        request.params.size() > 2) {
        throw std::runtime_error(RPCHelpMan{
            "scantxoutset",
            "\nEXPERIMENTAL warning: this call may be removed or changed "
            "in future releases.\n"
            "\nScans the unspent transaction output set for entries that "
            "match certain output descriptors.\n"
            "Examples of output descriptors are:\n"
            "    addr(<address>)                      Outputs whose "
            "scriptPubKey corresponds to the specified address (does not "
            "include P2PK)\n"
            "    raw(<hex script>)                    Outputs whose "
            "scriptPubKey equals the specified hex scripts\n"
            "    combo(<pubkey>)                      P2PK and P2PKH "
            "outputs for the given pubkey\n"
            "    pkh(<pubkey>)                        P2PKH outputs for "
            "the given pubkey\n"
            "    sh(multi(<n>,<pubkey>,<pubkey>,...)) P2SH-multisig "
            "outputs for the given threshold and pubkeys\n"
            "\nIn the above, <pubkey> either refers to a fixed public key "
            "in hexadecimal notation, or to an xpub/xprv optionally "
            "followed by one\n"
            "or more path elements separated by \"/\", and optionally "
            "ending in \"/*\" (unhardened), or \"/*'\" or \"/*h\" "
            "(hardened) to specify all\n"
            "unhardened or hardened child keys.\n"
            "In the latter case, a range needs to be specified by below if "
            "different from 1000.\n"
            "For more information on output descriptors, see the "
            "documentation in the doc/descriptors.md file.\n",
            {
                {"action", RPCArg::Type::STR, RPCArg::Optional::NO,
                 "The action to execute\n"
                 "                                      \"start\" for "
                 "starting a scan\n"
                 "                                      \"abort\" for "
                 "aborting the current scan (returns true when abort was "
                 "successful)\n"
                 "                                      \"status\" for "
                 "progress report (in %) of the current scan"},
                {"scanobjects",
                 RPCArg::Type::ARR,
                 RPCArg::Optional::NO,
                 "Array of scan objects\n"
                 "                                  Every scan object is "
                 "either a string descriptor or an object:",
                 {
                     {"descriptor", RPCArg::Type::STR,
                      RPCArg::Optional::OMITTED, "An output descriptor"},
                     {
                         "",
                         RPCArg::Type::OBJ,
                         RPCArg::Optional::OMITTED,
                         "An object with output descriptor and metadata",
                         {
                             {"desc", RPCArg::Type::STR, RPCArg::Optional::NO,
                              "An output descriptor"},
                             {"range", RPCArg::Type::NUM, /* default */ "1000",
                              "Up to what child index HD chains should be "
                              "explored"},
                         },
                     },
                 },
                 "[scanobjects,...]"},
            },
            RPCResult{
                "{\n"
                "  \"unspents\": [\n"
                "    {\n"
                "    \"txid\" : \"transactionid\",     (string) The "
                "transaction id\n"
                "    \"vout\": n,                    (numeric) the vout value\n"
                "    \"scriptPubKey\" : \"script\",    (string) the script "
                "key\n"
                "    \"desc\" : \"descriptor\",        (string) A specialized "
                "descriptor for the matched scriptPubKey\n"
                "    \"amount\" : x.xxx,             (numeric) The total "
                "amount in " +
                CURRENCY_UNIT +
                " of the unspent output\n"
                "    \"height\" : n,                 (numeric) Height of the "
                "unspent transaction output\n"
                "   }\n"
                "   ,...], \n"
                " \"total_amount\" : x.xxx,          (numeric) The total "
                "amount of all found unspent outputs in " +
                CURRENCY_UNIT +
                "\n"
                "]\n"},
            RPCExamples{""},
        }
                                     .ToString());
    }

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VARR});

    UniValue result(UniValue::VOBJ);
    if (request.params[0].get_str() == "status") {
        CoinsViewScanReserver reserver;
        if (reserver.reserve()) {
            // no scan in progress
            return NullUniValue;
        }
        result.pushKV("progress", g_scan_progress.load());
        return result;
    } else if (request.params[0].get_str() == "abort") {
        CoinsViewScanReserver reserver;
        if (reserver.reserve()) {
            // reserve was possible which means no scan was running
            return false;
        }
        // set the abort flag
        g_should_abort_scan = true;
        return true;
    } else if (request.params[0].get_str() == "start") {
        CoinsViewScanReserver reserver;
        if (!reserver.reserve()) {
            throw JSONRPCError(
                RPC_INVALID_PARAMETER,
                "Scan already in progress, use action \"abort\" or \"status\"");
        }
        std::set<CScript> needles;
        std::map<CScript, std::string> descriptors;
        Amount total_in = Amount::zero();

        // loop through the scan objects
        for (const UniValue &scanobject :
             request.params[1].get_array().getValues()) {
            std::string desc_str;
            int range = 1000;
            if (scanobject.isStr()) {
                desc_str = scanobject.get_str();
            } else if (scanobject.isObject()) {
                UniValue desc_uni = find_value(scanobject, "desc");
                if (desc_uni.isNull()) {
                    throw JSONRPCError(
                        RPC_INVALID_PARAMETER,
                        "Descriptor needs to be provided in scan object");
                }
                desc_str = desc_uni.get_str();
                UniValue range_uni = find_value(scanobject, "range");
                if (!range_uni.isNull()) {
                    range = range_uni.get_int();
                    if (range < 0 || range > 1000000) {
                        throw JSONRPCError(RPC_INVALID_PARAMETER,
                                           "range out of range");
                    }
                }
            } else {
                throw JSONRPCError(
                    RPC_INVALID_PARAMETER,
                    "Scan object needs to be either a string or an object");
            }

            FlatSigningProvider provider;
            auto desc = Parse(desc_str, provider);
            if (!desc) {
                throw JSONRPCError(
                    RPC_INVALID_ADDRESS_OR_KEY,
                    strprintf("Invalid descriptor '%s'", desc_str));
            }
            if (!desc->IsRange()) {
                range = 0;
            }
            for (int i = 0; i <= range; ++i) {
                std::vector<CScript> scripts;
                if (!desc->Expand(i, provider, scripts, provider)) {
                    throw JSONRPCError(
                        RPC_INVALID_ADDRESS_OR_KEY,
                        strprintf(
                            "Cannot derive script without private keys: '%s'",
                            desc_str));
                }
                for (const auto &script : scripts) {
                    std::string inferred =
                        InferDescriptor(script, provider)->ToString();
                    needles.emplace(script);
                    descriptors.emplace(std::move(script), std::move(inferred));
                }
            }
        }

        // Scan the unspent transaction output set for inputs
        UniValue unspents(UniValue::VARR);
        std::vector<CTxOut> input_txos;
        std::map<COutPoint, Coin> coins;
        g_should_abort_scan = false;
        g_scan_progress = 0;
        int64_t count = 0;
        std::unique_ptr<CCoinsViewCursor> pcursor;
        {
            LOCK(cs_main);
            ::ChainstateActive().ForceFlushStateToDisk();
            pcursor = std::unique_ptr<CCoinsViewCursor>(pcoinsdbview->Cursor());
            assert(pcursor);
        }
        bool res = FindScriptPubKey(g_scan_progress, g_should_abort_scan, count,
                                    pcursor.get(), needles, coins);
        result.pushKV("success", res);
        result.pushKV("searched_items", count);

        for (const auto &it : coins) {
            const COutPoint &outpoint = it.first;
            const Coin &coin = it.second;
            const CTxOut &txo = coin.GetTxOut();
            input_txos.push_back(txo);
            total_in += txo.nValue;

            UniValue unspent(UniValue::VOBJ);
            unspent.pushKV("txid", outpoint.GetTxId().GetHex());
            unspent.pushKV("vout", int32_t(outpoint.GetN()));
            unspent.pushKV("scriptPubKey", HexStr(txo.scriptPubKey.begin(),
                                                  txo.scriptPubKey.end()));
            unspent.pushKV("desc", descriptors[txo.scriptPubKey]);
            unspent.pushKV("amount", ValueFromAmount(txo.nValue));
            unspent.pushKV("height", int32_t(coin.GetHeight()));

            unspents.push_back(unspent);
        }
        result.pushKV("unspents", unspents);
        result.pushKV("total_amount", ValueFromAmount(total_in));
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid command");
    }
    return result;
}

// clang-format off
static const CRPCCommand commands[] = {
    //  category            name                      actor (function)        argNames
    //  ------------------- ------------------------  ----------------------  ----------
<<<<<<< HEAD
    { "blockchain",         "getblockchaininfo",      getblockchaininfo,        {} },
    { "blockchain",         "getchaintxstats",        &getchaintxstats,         {"nblocks", "blockhash"} },
    { "blockchain",         "getbestblockhash",       getbestblockhash,         {} },
    { "blockchain",         "getblockcount",          getblockcount,            {} },
    { "blockchain",         "getblock",               getblock,                 {"blockhash","verbose"} },
    { "blockchain",         "getblockdeltas",         getblockdeltas,           {} },
    { "blockchain",         "getblockhashes",         getblockhashes,           {} },
    { "blockchain",         "getblockhash",           getblockhash,             {"height"} },
    { "blockchain",         "getblockheader",         getblockheader,           {"blockhash","verbose"} },
    { "blockchain",         "getchaintips",           getchaintips,             {} },
    { "blockchain",         "getdifficulty",          getdifficulty,            {} },
    { "blockchain",         "getmempoolancestors",    getmempoolancestors,      {"txid","verbose"} },
    { "blockchain",         "getmempooldescendants",  getmempooldescendants,    {"txid","verbose"} },
    { "blockchain",         "getmempoolentry",        getmempoolentry,          {"txid"} },
    { "blockchain",         "getmempoolinfo",         getmempoolinfo,           {} },
    { "blockchain",         "getrawmempool",          getrawmempool,            {"verbose"} },
    { "blockchain",         "gettxout",               gettxout,                 {"txid","n","include_mempool"} },
    { "blockchain",         "gettxoutsetinfo",        gettxoutsetinfo,          {} },
    { "blockchain",         "pruneblockchain",        pruneblockchain,          {"height"} },
    { "blockchain",         "verifychain",            verifychain,              {"checklevel","nblocks"} },
    { "blockchain",         "preciousblock",          preciousblock,            {"blockhash"} },
=======
    { "blockchain",         "getbestblockhash",       getbestblockhash,       {} },
    { "blockchain",         "getblock",               getblock,               {"blockhash","verbosity|verbose"} },
    { "blockchain",         "getblockchaininfo",      getblockchaininfo,      {} },
    { "blockchain",         "getblockcount",          getblockcount,          {} },
    { "blockchain",         "getblockhash",           getblockhash,           {"height"} },
    { "blockchain",         "getblockheader",         getblockheader,         {"blockhash","verbose"} },
    { "blockchain",         "getblockstats",          getblockstats,          {"hash_or_height","stats"} },
    { "blockchain",         "getchaintips",           getchaintips,           {} },
    { "blockchain",         "getchaintxstats",        getchaintxstats,        {"nblocks", "blockhash"} },
    { "blockchain",         "getdifficulty",          getdifficulty,          {} },
    { "blockchain",         "getmempoolancestors",    getmempoolancestors,    {"txid","verbose"} },
    { "blockchain",         "getmempooldescendants",  getmempooldescendants,  {"txid","verbose"} },
    { "blockchain",         "getmempoolentry",        getmempoolentry,        {"txid"} },
    { "blockchain",         "getmempoolinfo",         getmempoolinfo,         {} },
    { "blockchain",         "getrawmempool",          getrawmempool,          {"verbose"} },
    { "blockchain",         "gettxout",               gettxout,               {"txid","n","include_mempool"} },
    { "blockchain",         "gettxoutsetinfo",        gettxoutsetinfo,        {} },
    { "blockchain",         "pruneblockchain",        pruneblockchain,        {"height"} },
    { "blockchain",         "savemempool",            savemempool,            {} },
    { "blockchain",         "verifychain",            verifychain,            {"checklevel","nblocks"} },
    { "blockchain",         "preciousblock",          preciousblock,          {"blockhash"} },
    { "blockchain",         "scantxoutset",           scantxoutset,           {"action", "scanobjects"} },
>>>>>>> abc/master

    /* Not shown in help */
    { "hidden",             "getfinalizedblockhash",            getfinalizedblockhash,            {} },
    { "hidden",             "finalizeblock",                    finalizeblock,                    {"blockhash"} },
    { "hidden",             "invalidateblock",                  invalidateblock,                  {"blockhash"} },
    { "hidden",             "parkblock",                        parkblock,                        {"blockhash"} },
    { "hidden",             "reconsiderblock",                  reconsiderblock,                  {"blockhash"} },
    { "hidden",             "syncwithvalidationinterfacequeue", syncwithvalidationinterfacequeue, {} },
    { "hidden",             "unparkblock",                      unparkblock,                      {"blockhash"} },
    { "hidden",             "waitfornewblock",                  waitfornewblock,                  {"timeout"} },
    { "hidden",             "waitforblock",                     waitforblock,                     {"blockhash","timeout"} },
    { "hidden",             "waitforblockheight",               waitforblockheight,               {"height","timeout"} },
};
// clang-format on

void RegisterBlockchainRPCCommands(CRPCTable &t) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++) {
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
    }
}
