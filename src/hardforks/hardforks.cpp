// Copyright (c) 2014-2024, The Monero Project
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "hardforks.h"

#undef DINASTYCOIN_DEFAULT_LOG_CATEGORY
#define DINASTYCOIN_DEFAULT_LOG_CATEGORY "blockchain.hardforks"

const hardfork_t mainnet_hard_forks[] = {
    // version 1 from the start of the blockchain
  { 1, 1, 0, 1532344521 },

  // version 2 starts from block 20,  . No fork voting occurs for the v2 fork.
  { 2, 20, 0, 1532345299 },

  // version 3 starts from block 40,  .
  { 3, 40, 0, 1532348216 },

  // version 4 starts from block 60,  .
  { 4, 60, 0, 1532351344 },

  // version 5 starts from block 80,  .
  { 5, 80, 0, 1532352706 },  

  // version 6 starts from block 100,  .
  { 6, 100, 0, 1532353879 },

  // version 7 starts from block 1546000,  .
  { 7, 120, 0, 1532356226 },

  // version 8 starts from block 72875,  
  { 8, 5800, 0, 1541572216 },

  // version 9 starts from block 73595,  
  { 9, 5850, 0, 1541846405 },
  // version 10 starts from block 77850
  { 10, 5900, 0, 1542911469 },
  { 11, 5950, 0, 1556115272 },
  { 12, 6000, 0, 1556201672 },
  
  //added code for harfork 13
  //version 13 starts from block 150000 , which is on or around the 19th of NOv, 2020. 
  { 13, 152500, 0, 1605752204 },
  // added harfork 14  // which is on or around the 1th of June, 2022. 
  { 14, 550000, 0, 1654089255 },
 { 17, 1512400, 0, 1767225600 },
  // HF18 TESLA369 v2 emergency difficulty and timestamp fix.
  { 18, 1605000, 0, 1785110400 },
};
const size_t num_mainnet_hard_forks = sizeof(mainnet_hard_forks) / sizeof(mainnet_hard_forks[0]);
const uint64_t mainnet_hard_fork_version_1_till = 19;

const hardfork_t testnet_hard_forks[] = {
  { 1, 1, 0, 1532344521 },
  { 2, 20, 0, 1532345299 },
  { 3, 40, 0, 1532348216 },
  { 4, 60, 0, 1532351344 },
  { 5, 80, 0, 1532352706 },  
  { 7, 120, 0, 1532356226 },
  { 8, 1000, 0, 1541572216 },
  { 9, 2000, 0, 1541846405 },
  { 10, 2100, 0, 1542911469 },
  { 11, 2200, 0, 1556115272 },
  { 12, 2300, 0, 1556201672 },
  { 13, 2350, 0, 1605752204 },
// includi HF14 (anche se non cambia difficulty) per coerenza col mainnet
  { 14, 2400, 0, 1654089255 },
  // il  big-bang: TESLA369 (Monero stable) a height vicino
  { 17, 3000, 0, 1767225600 },
  { 18, 3100, 0, 1785110400 },
};
const size_t num_testnet_hard_forks = sizeof(testnet_hard_forks) / sizeof(testnet_hard_forks[0]);
const uint64_t testnet_hard_fork_version_1_till = 19;

const hardfork_t stagenet_hard_forks[] = {
  // version 1 from the start of the blockchain
  { 1, 1, 0, 1341378000 },

  // versions 2-7 in rapid succession from March 13th, 2018
  { 2, 32000, 0, 1521000000 },
  { 3, 33000, 0, 1521120000 },
  { 4, 34000, 0, 1521240000 },
  { 5, 35000, 0, 1521360000 },
  { 6, 36000, 0, 1521480000 },
  { 7, 37000, 0, 1521600000 },
  { 8, 176456, 0, 1537821770 },
  { 9, 177176, 0, 1537821771 },
  { 10, 269000, 0, 1550153694 },
  { 11, 269720, 0, 1550225678 },
  { 12, 454721, 0, 1571419280 },
  { 13, 675405, 0, 1598180817 },
  { 14, 676125, 0, 1598180818 },
  { 15, 1151000, 0, 1656629117 },
  { 16, 1151720, 0, 1656629118 },
  { 17, 2, 0, 1767225600 },
  { 18, 3, 0, 1785110400 },
};
const size_t num_stagenet_hard_forks = sizeof(stagenet_hard_forks) / sizeof(stagenet_hard_forks[0]);
