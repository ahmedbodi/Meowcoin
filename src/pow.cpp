// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2020 The Meowcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pow.h"

#include "arith_uint256.h"
#include "chain.h"
#include "primitives/block.h"
#include "uint256.h"
#include "util.h"
#include "validation.h"
#include "chainparams.h"
#include "tinyformat.h"

unsigned int static DarkGravityWave(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params) {
    /* current difficulty formula, dash - DarkGravity v3, written by Evan Duffield - evan@dash.org */
    assert(pindexLast != nullptr);

    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    int64_t nPastBlocks = 180; // ~3hr

    // make sure we have at least (nPastBlocks + 1) blocks, otherwise just return powLimit
    if (!pindexLast || pindexLast->nHeight < nPastBlocks) {
        return bnPowLimit.GetCompact();
    }

    if (params.fPowAllowMinDifficultyBlocks && params.fPowNoRetargeting) {
        // Special difficulty rule:
        // If the new block's timestamp is more than 2 * 1 minutes
        // then allow mining of a min-difficulty block.
        if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing * 2)
            return nProofOfWorkLimit;
        else {
            // Return the last non-special-min-difficulty-rules-block
            const CBlockIndex *pindex = pindexLast;
            while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval() != 0 &&
                   pindex->nBits == nProofOfWorkLimit)
                pindex = pindex->pprev;
            return pindex->nBits;
        }
    }

    const CBlockIndex *pindex = pindexLast;
    arith_uint256 bnPastTargetAvg;

    int nKAWPOWBlocksFound = 0;
    int nMEOWPOWBlocksFound = 0;
    int nAuxPOWBlocksFound = 0;  // Track AuxPOW blocks
    
    for (unsigned int nCountBlocks = 1; nCountBlocks <= nPastBlocks; nCountBlocks++) {
        arith_uint256 bnTarget = arith_uint256().SetCompact(pindex->nBits);
        if (nCountBlocks == 1) {
            bnPastTargetAvg = bnTarget;
        } else {
            // NOTE: that's not an average really...
            bnPastTargetAvg = (bnPastTargetAvg * nCountBlocks + bnTarget) / (nCountBlocks + 1);
        }

        // Count how blocks are KAWPOW mined in the last 180 blocks
        if (pindex->nTime >= nKAWPOWActivationTime && pindex->nTime < nMEOWPOWActivationTime) {
            nKAWPOWBlocksFound++;
        }

        // Count how blocks are MEOWPOW mined in the last 180 blocks
        if (pindex->nTime >= nMEOWPOWActivationTime && !pindex->nVersion.IsAuxpow()) {
            nMEOWPOWBlocksFound++;
        }
        
        // Count how blocks are AuxPOW (Scrypt) mined in the last 180 blocks
        if (pindex->nVersion.IsAuxpow()) {
            nAuxPOWBlocksFound++;
        }

        if(nCountBlocks != nPastBlocks) {
            assert(pindex->pprev); // should never fail
            pindex = pindex->pprev;
        }
    }

    // Always print block distribution stats to make it very visible for debugging
    LogPrintf("==== MINING DISTRIBUTION STATS at height %d ====\n", pindexLast->nHeight + 1);
    LogPrintf("MeowPOW blocks: %d (%.2f%%)\n", nMEOWPOWBlocksFound, (double)nMEOWPOWBlocksFound * 100.0 / nPastBlocks);
    LogPrintf("AuxPOW blocks: %d (%.2f%%)\n", nAuxPOWBlocksFound, (double)nAuxPOWBlocksFound * 100.0 / nPastBlocks);
    LogPrintf("KAWPOW blocks: %d (%.2f%%)\n", nKAWPOWBlocksFound, (double)nKAWPOWBlocksFound * 100.0 / nPastBlocks);
    LogPrintf("Target ratio: 50/50 MeowPOW/AuxPOW\n");
    LogPrintf("==== END STATS ====\n");

    // If we are mining a KAWPOW block. We check to see if we have mined
    // 180 KAWPOW blocks already. If we haven't we are going to return our
    // temp limit. This will allow us to change algos to kawpow without having to
    // change the DGW math.
    if (pblock->nTime >= nKAWPOWActivationTime && pblock->nTime < nMEOWPOWActivationTime) {
        if (nKAWPOWBlocksFound != nPastBlocks) {
            const arith_uint256 bnKawPowLimit = UintToArith256(params.kawpowLimit);
            return bnKawPowLimit.GetCompact();
        }
    }

    //Meowpow
    if (pblock->nTime >= nMEOWPOWActivationTime && !pblock->nVersion.IsAuxpow()) {
        if (nMEOWPOWBlocksFound != nPastBlocks) {
            const arith_uint256 bnMeowPowLimit = UintToArith256(params.meowpowLimit);
            return bnMeowPowLimit.GetCompact();
        }
    }
    
    //AuxPOW (Scrypt)
    if (pblock->nVersion.IsAuxpow()) {
        // Calculate the target ratio between AuxPOW and MeowPOW
        // Ideally, we want approximately 50/50 distribution of blocks
        
        // Get the percentage of AuxPOW blocks in the past window
        double auxPowPercentage = (double)nAuxPOWBlocksFound / nPastBlocks;
        
        // Calculate adjustment factor to bring us closer to 50/50
        // If we have more than 50% AuxPOW blocks, increase difficulty
        // If we have less than 50% AuxPOW blocks, decrease difficulty
        double targetPercentage = 0.5; // We want 50% AuxPOW blocks
        double adjustmentFactor = 1.0;
        
        if (auxPowPercentage > 0) {
            adjustmentFactor = targetPercentage / auxPowPercentage;
            
            // Limit extreme adjustments
            if (adjustmentFactor > 4.0) adjustmentFactor = 4.0;
            if (adjustmentFactor < 0.25) adjustmentFactor = 0.25;
        }
        
        // Log the difficulty adjustment for AuxPOW
        LogPrintf("AuxPOW difficulty adjustment: current percentage=%.2f%%, target=%.2f%%, adjustment factor=%.4f\n", 
                 auxPowPercentage * 100.0, targetPercentage * 100.0, adjustmentFactor);
        
        // Adjust the target based on our calculated factor
        arith_uint256 bnNew(bnPastTargetAvg);
        bnNew *= adjustmentFactor;
        
        // Make sure we don't exceed the proof of work limit
        if (bnNew > bnPowLimit) {
            bnNew = bnPowLimit;
        }
        
        // Log the final difficulty target
        LogPrintf("AuxPOW new target: %s (bits=%08x)\n", 
                 bnNew.ToString(), bnNew.GetCompact());
        
        return bnNew.GetCompact();
    }

    arith_uint256 bnNew(bnPastTargetAvg);

    int64_t nActualTimespan = pindexLast->GetBlockTime() - pindex->GetBlockTime();
    // NOTE: is this accurate? nActualTimespan counts it for (nPastBlocks - 1) blocks only...
    int64_t nTargetTimespan = nPastBlocks * params.nPowTargetSpacing;

    if (nActualTimespan < nTargetTimespan/3)
        nActualTimespan = nTargetTimespan/3;
    if (nActualTimespan > nTargetTimespan*3)
        nActualTimespan = nTargetTimespan*3;

    // Retarget
    bnNew *= nActualTimespan;
    bnNew /= nTargetTimespan;

    if (bnNew > bnPowLimit) {
        bnNew = bnPowLimit;
    }

    return bnNew.GetCompact();
}

unsigned int GetNextWorkRequiredBTC(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    // Only change once per difficulty adjustment interval
    if ((pindexLast->nHeight+1) % params.DifficultyAdjustmentInterval() != 0)
    {
        if (params.fPowAllowMinDifficultyBlocks)
        {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 2* 10 minutes
            // then allow mining of a min-difficulty block.
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*2)
                return nProofOfWorkLimit;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval() != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    // Go back by what we want to be 14 days worth of blocks
    int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval()-1);
    assert(nHeightFirst >= 0);
    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    assert(pindexFirst);

    return CalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
//    int64_t nPrevBlockTime = (pindexLast->pprev ? pindexLast->pprev->GetBlockTime() : pindexLast->GetBlockTime());  //<- Commented out - fixes "not used" warning

    if (IsDGWActive(pindexLast->nHeight + 1)) {
//        LogPrint(BCLog::NET, "Block %s - version: %s: found next work required using DGW: [%s] (BTC would have been [%s]\t(%+d)\t(%0.3f%%)\t(%s sec))\n",
//                 pindexLast->nHeight + 1, pblock->nVersion, dgw, btc, btc - dgw, (float)(btc - dgw) * 100.0 / (float)dgw, pindexLast->GetBlockTime() - nPrevBlockTime);
        return DarkGravityWave(pindexLast, pblock, params);
    }
    else {
//        LogPrint(BCLog::NET, "Block %s - version: %s: found next work required using BTC: [%s] (DGW would have been [%s]\t(%+d)\t(%0.3f%%)\t(%s sec))\n",
//                  pindexLast->nHeight + 1, pblock->nVersion, btc, dgw, dgw - btc, (float)(dgw - btc) * 100.0 / (float)btc, pindexLast->GetBlockTime() - nPrevBlockTime);
        return GetNextWorkRequiredBTC(pindexLast, pblock, params);
    }

}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < params.nPowTargetTimespan/4)
        nActualTimespan = params.nPowTargetTimespan/4;
    if (nActualTimespan > params.nPowTargetTimespan*4)
        nActualTimespan = params.nPowTargetTimespan*4;

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    bnNew *= nActualTimespan;
    bnNew /= params.nPowTargetTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow)
        return false;

    // Use the right limit based on the bits value
    // The nBits value already encodes which algorithm we're using
    if (bnTarget > UintToArith256(params.powLimit))
        return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget) {
        LogPrint(BCLog::AUXPOW, "CheckProofOfWork: hash %s is greater than target %s\n", 
                hash.ToString(), bnTarget.ToString());
        return false;
    }

    return true;
}
