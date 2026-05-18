// Parts are Copyright (c) 2019, The Dinastycoin team
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
//
// Parts of this file are originally copyright (c) 2015-2019 The Monero Project


#include "checkpoints.h"

#include "common/dns_utils.h"
#include "string_tools.h"
#include "storages/portable_storage_template_helper.h" // epee json include
#include "serialization/keyvalue_serialization.h"
#include <boost/system/error_code.hpp>
#include <boost/filesystem.hpp>
#include <functional>
#include <vector>

using namespace epee;

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "checkpoints"

namespace cryptonote
{
  /**
   * @brief struct for loading a checkpoint from json
   */
  struct t_hashline
  {
    uint64_t height; //!< the height of the checkpoint
    std::string hash; //!< the hash for the checkpoint
        BEGIN_KV_SERIALIZE_MAP()
          KV_SERIALIZE(height)
          KV_SERIALIZE(hash)
        END_KV_SERIALIZE_MAP()
  };

  /**
   * @brief struct for loading many checkpoints from json
   */
  struct t_hash_json {
    std::vector<t_hashline> hashlines; //!< the checkpoint lines from the file
        BEGIN_KV_SERIALIZE_MAP()
          KV_SERIALIZE(hashlines)
        END_KV_SERIALIZE_MAP()
  };

  //---------------------------------------------------------------------------
  checkpoints::checkpoints()
  {
  }
  //---------------------------------------------------------------------------
  bool checkpoints::add_checkpoint(uint64_t height, const std::string& hash_str, const std::string& difficulty_str)
  {
    crypto::hash h = crypto::null_hash;
    bool r = epee::string_tools::hex_to_pod(hash_str, h);
    CHECK_AND_ASSERT_MES(r, false, "Failed to parse checkpoint hash string into binary representation!");

    // return false if adding at a height we already have AND the hash is different
    if (m_points.count(height))
    {
      CHECK_AND_ASSERT_MES(h == m_points[height], false, "Checkpoint at given height already exists, and hash for new checkpoint was different!");
    }
    m_points[height] = h;
    if (!difficulty_str.empty())
    {
      try
      {
        difficulty_type difficulty(difficulty_str);
        if (m_difficulty_points.count(height))
        {
          CHECK_AND_ASSERT_MES(difficulty == m_difficulty_points[height], false, "Difficulty checkpoint at given height already exists, and difficulty for new checkpoint was different!");
        }
        m_difficulty_points[height] = difficulty;
          // Validate monotonicity: cumulative difficulty must always increase
        if (m_difficulty_points.size() >= 2) {
            auto it = m_difficulty_points.find(height);
            if (it != m_difficulty_points.begin()) {
                auto prev_it = std::prev(it);
                if (height > prev_it->first && difficulty <= prev_it->second) {
                    LOG_ERROR("Non-monotonic cumulative difficulty at height " << height
                              << ": new=" << difficulty
                              << " <= prev=" << prev_it->second
                              << " at h." << prev_it->first);
                    m_difficulty_points.erase(it);
                    return false;
                }
            }
        }
  
      }
      catch (...)
      {
        LOG_ERROR("Failed to parse difficulty checkpoint: " << difficulty_str);
        return false;
      }
    }
    return true;
  }
  //---------------------------------------------------------------------------
  bool checkpoints::is_in_checkpoint_zone(uint64_t height) const
  {
    return !m_points.empty() && (height <= (--m_points.end())->first);
  }
  //---------------------------------------------------------------------------
  bool checkpoints::check_block(uint64_t height, const crypto::hash& h, bool& is_a_checkpoint) const
  {
    auto it = m_points.find(height);
    is_a_checkpoint = it != m_points.end();
    if(!is_a_checkpoint)
      return true;

    if(it->second == h)
    {
      MINFO("CHECKPOINT PASSED FOR HEIGHT " << height << " " << h);
      return true;
    }else
    {
      MWARNING("CHECKPOINT FAILED FOR HEIGHT " << height << ". EXPECTED HASH: " << it->second << ", FETCHED HASH: " << h);
      return false;
    }
  }
  //---------------------------------------------------------------------------
  bool checkpoints::check_block(uint64_t height, const crypto::hash& h) const
  {
    bool ignored;
    return check_block(height, h, ignored);
  }
  //---------------------------------------------------------------------------
  //FIXME: is this the desired behavior?
  bool checkpoints::is_alternative_block_allowed(uint64_t blockchain_height, uint64_t block_height) const
  {
    if (0 == block_height)
      return false;

    auto it = m_points.upper_bound(blockchain_height);
    // Is blockchain_height before the first checkpoint?
    if (it == m_points.begin())
      return true;

    --it;
    uint64_t checkpoint_height = it->first;
    return checkpoint_height < block_height;
  }
  //---------------------------------------------------------------------------
  uint64_t checkpoints::get_max_height() const
  {
    if (m_points.empty())
      return 0;
    return m_points.rbegin()->first;
  }
  //---------------------------------------------------------------------------
  uint64_t checkpoints::get_nearest_checkpoint_height(uint64_t block_height) const
  {
    if (m_points.empty())
      return 0;

    auto it = m_points.upper_bound(block_height);
    if (it == m_points.begin())
      return 0;

    --it;
    return it->first;
  }
  //---------------------------------------------------------------------------
  const std::map<uint64_t, crypto::hash>& checkpoints::get_points() const
  {
    return m_points;
  }
  //---------------------------------------------------------------------------
  const std::map<uint64_t, difficulty_type>& checkpoints::get_difficulty_points() const
  {
    return m_difficulty_points;
  }

  bool checkpoints::check_for_conflicts(const checkpoints& other) const
  {
    for (auto& pt : other.get_points())
    {
      if (m_points.count(pt.first))
      {
        CHECK_AND_ASSERT_MES(pt.second == m_points.at(pt.first), false, "Checkpoint at given height already exists, and hash for new checkpoint was different!");
      }
    }
    return true;
  }

  bool checkpoints::init_default_checkpoints(network_type nettype)
  {
    if (nettype == TESTNET)
    {

      return true;
    }
    if (nettype == STAGENET)
    {

      return true;
    }
       ADD_CHECKPOINT2(1,    "7b9b6064c13231bda96759fcabd21255af66f94ddece53695509ecb528479667", "");
            ADD_CHECKPOINT2(10 , "7fff6b1b180abe1ade902232b0d39372dd165e82addd0a12514b69115ab29789", "");
            ADD_CHECKPOINT2(100, "b922e51c7cccba7f7fd12b395b942a6092566c47879862b127405dc16c3b415a", "");
            ADD_CHECKPOINT2(500, "4161494672a7ef39e1a1c6d5e4b3c6e899b5a945cd1dd7239ad734189c663f29", "");
            ADD_CHECKPOINT2(1000, "f75b44cbf1f070814ae83bb54d0d0b98ee0583633ed88b21088a3957ccb675c0", "");
            ADD_CHECKPOINT2(2000, "a739216d63de35fa69c74ff22c2ed201fd2d0dbe7c38a8bbdbb64368fd18aff1", "");
            ADD_CHECKPOINT2(3000, "0d5882e703a4e715450cc2538ead37d2ad2960c0ad9245546187c04b11ae5b4c", "");
            ADD_CHECKPOINT2(4000, "d66aee31dff6b06f5d6f56fdaab71247325b818968c3c555f6626969965487eb", "");
            ADD_CHECKPOINT2(5000, "458bf83117978a24c16e77419d450e81dc808ed8288e3ff301f3e9ff41520b0a", "");
            ADD_CHECKPOINT2(5353, "e96ad3449cec0f97978f1c79120d713c1753116d778b33c6d5609bed99fdd2a7", "");
            ADD_CHECKPOINT2(5500, "58cea8b62686f3a3c0c8f9edd30b02810cad1033ad2eea05fe47f63f0838a460", "");
            ADD_CHECKPOINT2(5544, "963e97cad472b7ab43676129d7eb87c0791ee0f160634ea7d26b02f29230c740", "");
            ADD_CHECKPOINT2(6000, "50f4c25ab0997c79f47b32aa7a766a3821e5e40935d46e03260ca1a913138df1", "");
            ADD_CHECKPOINT2(6500, "f26226611fcd1437882f1a3a484cc8823ea59d009cace890620c093b587b4487", "");
            ADD_CHECKPOINT2(7000, "522b3f918a3976bf79b4802aba906c318880d73daef5e8a3d168b59096a43f3c", "");
            ADD_CHECKPOINT2(8000, "ee949fccb6f4db661f5a38e4c8f487dbaf5bd18bacfb4d77b32eb3bc3abb7794", "");
            ADD_CHECKPOINT2(9500, "b62d0dae7be7012138af83244160797389fffb3ef2aae2ec3d91082b1a58a047", "");
            ADD_CHECKPOINT2(10000,"92388506769d6ee510af6f480099a1f5466a6cae855bb5c51e0bb328457cd5d4", "");
            ADD_CHECKPOINT2(12000,"63554dd0ae6f178f5a8bb94232e5004cae09d3d797d0953c48d0cd93b6b3743c", "");
            ADD_CHECKPOINT2(15622,"189a796e8fb84bdcca69cf8dc2336f0d652a11504dc9c8b5da7f217ae331e867", "");
            ADD_CHECKPOINT2(20000, "5507b571ba1f634810627ca2a8450b894d474762cffd79ddbfaefee3b96f22a5", "");
            ADD_CHECKPOINT2(32139,"b6bb051810a65fdf20c12b8b847e306e670861abeecbfb126b7eb3be55f559ac", "");
            ADD_CHECKPOINT2(39638, "e8d7e2d5389ed04e6beaa53dbc6707a47e76d8f86f074a434ff2e4ff74cda5f3", "");
            ADD_CHECKPOINT2(152000, "baad73b2f169343d7e6e3d47f554256e1c5325fc14c4e3d02ac77f3e53b885e6", "");
            ADD_CHECKPOINT2(152200, "11971b757ac2290b1940e8fa4885f045ea904746ff386e0dcd9f4becc10f3724", "");
            ADD_CHECKPOINT2(152250, "64709667a4591058d1c294697050d1b30fc7a343604436fbede959eefa1640f8", "");
            ADD_CHECKPOINT2(152251, "ef71e9ed07acc074402403fdfef3bf11c0bc3bcb3780d3fb7e1034e0422bf96b", "");
            ADD_CHECKPOINT2(152300, "da3fd3186d559ef8a3d2ba10670edf36cbdc4f462edfcfeb015d39ca34fbf9a6", "");
            ADD_CHECKPOINT2(200000, "76d158c401cba51a35d35b5141b7c3bbdca0447431d8714c9797cb30f711e9ff", "");
            ADD_CHECKPOINT2(226000, "d4e076d8a4c23e6e51df50ae038f710fe83b1363c69b5d6c94c3d227912ff10c", "");
            ADD_CHECKPOINT2(263664, "3ea3ebf33bc4c73b00d28addabdf47ca2bf9b0a202f2646a01f5a9121e5d3a54", "");
            ADD_CHECKPOINT2(300000,  "8c5a9f86b20861c1dee6ab90ac86d0b1816163c11f5cf8e23566157e36043998", "");
         ADD_CHECKPOINT2(400000, "5ea6a74691c402be4f428954c00c9b9359a9a1f9afac1317e8115cf793efa039", "");
         ADD_CHECKPOINT2(500000, "0dbc3dbb1a91236aceef5a7099d5ce07b255648d84738c09bdf9bfd10fa2e44a", "");
         ADD_CHECKPOINT2(600000, "0a762b3e457ecdc67bd14284aad87844d60f7449366843eca6758c3c82f77c7c", "");
         ADD_CHECKPOINT2(650000, "ee01635e35376b62883cf502917cc1b7f4c1343916a50a1a14b20651c465a243", "");
         ADD_CHECKPOINT2(666666, "462b294427f8866bede3ff041b94dc6fac31ea436dc5048f2d1a589b9ff40dce", "");
         ADD_CHECKPOINT2(700000, "d406c6a4e55fc31fc0f9e26a1afea72125a5232aabb4268ea4cb7bc923cba6ea", "");
         ADD_CHECKPOINT2(777777,   "f9229a8c352d04f32d66314cccdc16eae524db6f76dd0297faf51632de090981", "");
         ADD_CHECKPOINT2(835123, "3a0cbe0745f8c9d4c9d01fa047de4a8751b8a18e4629a503bc00945c6f254de4", "");
         ADD_CHECKPOINT2(900000,   "636fbd07ed37e6898f44c90f64d455cbd6313dcb4c0516674f37fc13184ef65c", "");
         ADD_CHECKPOINT2(1000000,  "4f2702ebdd1c8698e4f9eb4ea0a1082845fbad3e5ce3993121594bf66c8bf405", "");
         ADD_CHECKPOINT2(1111111,    "1d6d29f3a27847ec3836a65eb29977e5b8fda03430a27e49f7be8ffb0b81b01c", "");
         ADD_CHECKPOINT2(1200000,  "0d348de52ab334632f5fd11346728c865fec2444c1c1fd38979d5be8086dcb0f", "");
         ADD_CHECKPOINT2(1222222,    "ae92b5c9eddbaf8ea2c5a759078781d375f4992f082ae90d1d18bde8e4cda647", "");
         ADD_CHECKPOINT2(1300000,   "a55a2233e036fecf6c26fdcb593857040b5705718169669d6b870f76249e9201", "");
         ADD_CHECKPOINT2(1400000,  "38b004a89d0a4573d69c446edd249f6473e20ececa5e9e42d4b5eb44e4b48e16", "");
         ADD_CHECKPOINT2(1447206,  "b8d0bea2ab54a1740b14d139709b9c72f7e0911a4342c91b5e9964cfac7f0abe", "");  
         ADD_CHECKPOINT2(1455513,  "38c5ebc4d156f30754fa2a76dbeac1def3f370d4069f997153c50dbdeb7916fb", "");  
         ADD_CHECKPOINT2(1480000, "e2ad271829a9f9002cc7390140ebc594bd9864f086ee89b66d6ddb8cccf6a54f", "");
         ADD_CHECKPOINT2(1500000, "b1557ccca36504b6e3153eef788c4f2b0143ff1ea33b31d141865ffbf495d0ba", ""); 
// HF17 (TESLA369) activation block — correct cumulative_difficulty from live seed node
         ADD_CHECKPOINT2(1512400, "76385820657177c29585e155a01e60f2139064e7a60ae03981c2938f0781b74a", "0x1dd841a8c96b");
         // First block after checkpoints.dat fast-sync boundary — triggers recalculate_difficulties if diverged
         // recalculate_difficulties() will stop at (last_cp_with_cumdiff - 1) = h.1514178, NOT overwriting HF17+ blocks
         ADD_CHECKPOINT2(1514178, "6573a50fc4b0599dd0f1416176185ceab54f2a593db084a635b2281beb4c45a9", "0x1dd841ac6b39");
         ADD_CHECKPOINT2(1514179, "e3c623ebd843cef44e55fa271fd3de710dc4b5c0615150d5f8196cf4147516fd", "0x1dd841ac6c50");  
         ADD_CHECKPOINT2(1515219, "c56ee46aa1ab0291217b72bb38ba44080b458a35b1e7d2e78c6d704abd4e8675", "");
         ADD_CHECKPOINT2(1518738, "430e0ec89eb97bac831cd721fac68ad1233e8525039a5fdca41274f1bbac1171", "");     
         ADD_CHECKPOINT2(1521000, "1274d51fd9f156caec28da1ed47e46ba10edb09bef38bd0e8669d80cbecec3c7", "");
         ADD_CHECKPOINT2(1531437, "ceda188ab2f0aa5ac9f6ecab2d40ceb0846a3373b5e253a999dcc8b26903c831", "");
         ADD_CHECKPOINT2(1531438, "69898edea77f3a93351945d36734b5a91625491972baee6805c64cfa2c3652e0", "");
         ADD_CHECKPOINT2(1531547, "46a74e14274da2b3812d63fc451af5c23d413bff6cfb764087c1ca272a0cab54", "");
         ADD_CHECKPOINT2(1531557, "07354b348c674ab6f001ae6a2149bf164e75307a5d20f9fae1f1292f4c58f45c", "");
         ADD_CHECKPOINT2(1536933, "99ef9d3a4a38a259d0850269e4d4e7308613b48d967d1a1b24816eed4d18b325", "");
         ADD_CHECKPOINT2(1536983, "93a43530ed5a08edaddfac61dbde2f51f0737ce80da28cbe0e0a3586ab0c9726", "");
         ADD_CHECKPOINT2(1536984, "46417c0bf6e16da51b6aff1cd73380fdba060978fcbc27a651321d85f664eb7c", "");
         
         return true;
  }         



  bool checkpoints::load_checkpoints_from_json(const std::string &json_hashfile_fullpath)
  {
    boost::system::error_code errcode;
    if (! (boost::filesystem::exists(json_hashfile_fullpath, errcode)))
    {
      LOG_PRINT_L1("Blockchain checkpoints file not found");
      return true;
    }

    LOG_PRINT_L1("Adding checkpoints from blockchain hashfile");

    uint64_t prev_max_height = get_max_height();
    LOG_PRINT_L1("Hard-coded max checkpoint height is " << prev_max_height);
    t_hash_json hashes;
    if (!epee::serialization::load_t_from_json_file(hashes, json_hashfile_fullpath))
    {
      MERROR("Error loading checkpoints from " << json_hashfile_fullpath);
      return false;
    }
    for (std::vector<t_hashline>::const_iterator it = hashes.hashlines.begin(); it != hashes.hashlines.end(); )
    {
      uint64_t height;
      height = it->height;
      if (height <= prev_max_height) {
	LOG_PRINT_L1("ignoring checkpoint height " << height);
      } else {
	std::string blockhash = it->hash;
	LOG_PRINT_L1("Adding checkpoint height " << height << ", hash=" << blockhash);
	ADD_CHECKPOINT(height, blockhash);
      }
      ++it;
    }

    return true;
  }

  bool checkpoints::load_checkpoints_from_dns(network_type nettype)
  {
    std::vector<std::string> records;

    // All four DinastycoinPulse domains have DNSSEC on and valid
    static const std::vector<std::string> dns_urls = {
   // DNS checkpoint seeds disabilitati: uso solo checkpoint hardcoded
};


    static const std::vector<std::string> testnet_dns_urls = {
       "seed4.dinastycoin.com"
};


    static const std::vector<std::string> stagenet_dns_urls = { "stagenetpoints1.dinastycoin.com"
                   , "stagenetpoints2.dinastycoin.com"
                   , "stagenetpoints3.dinastycoin.com"
                   , "stagenetpoints4.dinastycoin.com"
    };

 if (!tools::dns_utils::load_txt_records_from_dns(records, nettype == TESTNET ? testnet_dns_urls : nettype == STAGENET ? stagenet_dns_urls : dns_urls))
      return true; // why true ?

    for (const auto& record : records)
    {
      auto pos = record.find(":");
      if (pos != std::string::npos)
      {
        uint64_t height;
        crypto::hash hash;

        // parse the first part as uint64_t,
        // if this fails move on to the next record
        std::stringstream ss(record.substr(0, pos));
        if (!(ss >> height))
        {
    continue;
        }

        // parse the second part as crypto::hash,
        // if this fails move on to the next record
        std::string hashStr = record.substr(pos + 1);
        if (!epee::string_tools::hex_to_pod(hashStr, hash))
        {
    continue;
        }

        ADD_CHECKPOINT(height, hashStr);
      }
    }
    return true;
  }

  bool checkpoints::load_new_checkpoints(const std::string &json_hashfile_fullpath, network_type nettype, bool dns)
  {
    bool result;

    result = load_checkpoints_from_json(json_hashfile_fullpath);
    if (dns)
    {
      result &= load_checkpoints_from_dns(nettype);
    }

    return result;
  }
}

