// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Copyright (c) 2023-2024 The Meowcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MEOWCOIN_PATCHCONFIG_H
#define MEOWCOIN_PATCHCONFIG_H

/**
 * Configuration for auxpow implementation
 * The following macros provide conditional compilation options
 * for the auxpow implementation and can be modified for debugging.
 */

/**
 * Enable enhanced logging for auxpow validation
 * When enabled, additional diagnostic information will be logged during auxpow validation.
 */
#define AUXPOW_ENABLE_LOGGING 1

/**
 * Skip parent block proof of work validation. This is useful for debugging
 * and initial auxpow implementation.
 */
#define AUXPOW_SKIP_POW_CHECK 0

/**
 * Check detailed auxpow requirements including presence of merkle root
 * in coinbase. Set to 0 for less strict validation during debugging.
 */
#define AUXPOW_STRICT_VALIDATION 1

/**
 * Accept parent block's own difficulty target instead of requiring child's difficulty.
 * This is more realistic for merge-mined chains where the parent chain might have
 * a different difficulty scale.
 */
#define AUXPOW_USE_PARENT_DIFFICULTY 0

#endif // MEOWCOIN_PATCHCONFIG_H