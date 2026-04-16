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
       ADD_CHECKPOINT2(1,    "7b9b6064c13231bda96759fcabd21255af66f94ddece53695509ecb528479667", "0x2");
            ADD_CHECKPOINT2(10 , "7fff6b1b180abe1ade902232b0d39372dd165e82addd0a12514b69115ab29789", "0x421");
            ADD_CHECKPOINT2(100, "b922e51c7cccba7f7fd12b395b942a6092566c47879862b127405dc16c3b415a", "0x195dd");
            ADD_CHECKPOINT2(500, "4161494672a7ef39e1a1c6d5e4b3c6e899b5a945cd1dd7239ad734189c663f29", "0x245946");
            ADD_CHECKPOINT2(1000, "f75b44cbf1f070814ae83bb54d0d0b98ee0583633ed88b21088a3957ccb675c0", "0x57aff1");
            ADD_CHECKPOINT2(2000, "a739216d63de35fa69c74ff22c2ed201fd2d0dbe7c38a8bbdbb64368fd18aff1", "0xe1bbf9");
            ADD_CHECKPOINT2(3000, "0d5882e703a4e715450cc2538ead37d2ad2960c0ad9245546187c04b11ae5b4c", "0x17b2e85");
            ADD_CHECKPOINT2(4000, "d66aee31dff6b06f5d6f56fdaab71247325b818968c3c555f6626969965487eb", "0x34a1b5f");
            ADD_CHECKPOINT2(5000, "458bf83117978a24c16e77419d450e81dc808ed8288e3ff301f3e9ff41520b0a", "0x6fd2729");
            ADD_CHECKPOINT2(5353, "e96ad3449cec0f97978f1c79120d713c1753116d778b33c6d5609bed99fdd2a7", "0x8e05d81");
            ADD_CHECKPOINT2(5500, "58cea8b62686f3a3c0c8f9edd30b02810cad1033ad2eea05fe47f63f0838a460", "0x983fbc6");
            ADD_CHECKPOINT2(5544, "963e97cad472b7ab43676129d7eb87c0791ee0f160634ea7d26b02f29230c740", "0x9a6e7ca");
            ADD_CHECKPOINT2(6000, "50f4c25ab0997c79f47b32aa7a766a3821e5e40935d46e03260ca1a913138df1", "0xabcad5e");
            ADD_CHECKPOINT2(6500, "f26226611fcd1437882f1a3a484cc8823ea59d009cace890620c093b587b4487", "0xb9bc42b");
            ADD_CHECKPOINT2(7000, "522b3f918a3976bf79b4802aba906c318880d73daef5e8a3d168b59096a43f3c", "0x10739433");
            ADD_CHECKPOINT2(8000, "ee949fccb6f4db661f5a38e4c8f487dbaf5bd18bacfb4d77b32eb3bc3abb7794", "0x3ad1ce2f");
            ADD_CHECKPOINT2(9500, "b62d0dae7be7012138af83244160797389fffb3ef2aae2ec3d91082b1a58a047", "0x8ceae7f3");
            ADD_CHECKPOINT2(10000,"92388506769d6ee510af6f480099a1f5466a6cae855bb5c51e0bb328457cd5d4", "0xa744dff6");
            ADD_CHECKPOINT2(12000,"63554dd0ae6f178f5a8bb94232e5004cae09d3d797d0953c48d0cd93b6b3743c", "0x196f830ca");
            ADD_CHECKPOINT2(15622,"189a796e8fb84bdcca69cf8dc2336f0d652a11504dc9c8b5da7f217ae331e867", "0xdf07ed99d");
            ADD_CHECKPOINT2(20000, "5507b571ba1f634810627ca2a8450b894d474762cffd79ddbfaefee3b96f22a5", "0x47e70ab047");
            ADD_CHECKPOINT2(32139,"b6bb051810a65fdf20c12b8b847e306e670861abeecbfb126b7eb3be55f559ac", "0x104d075c0b4");
            ADD_CHECKPOINT2(39638, "e8d7e2d5389ed04e6beaa53dbc6707a47e76d8f86f074a434ff2e4ff74cda5f3", "0x17f20fb70cb");
            ADD_CHECKPOINT2(152000, "baad73b2f169343d7e6e3d47f554256e1c5325fc14c4e3d02ac77f3e53b885e6", "0x15cbba7f3b7c");
            ADD_CHECKPOINT2(152200, "11971b757ac2290b1940e8fa4885f045ea904746ff386e0dcd9f4becc10f3724", "0x16076395601b");
            ADD_CHECKPOINT2(152250, "64709667a4591058d1c294697050d1b30fc7a343604436fbede959eefa1640f8", "0x161077f21a7b");
            ADD_CHECKPOINT2(152251, "ef71e9ed07acc074402403fdfef3bf11c0bc3bcb3780d3fb7e1034e0422bf96b", "0x1610a6adc037");
            ADD_CHECKPOINT2(152300, "da3fd3186d559ef8a3d2ba10670edf36cbdc4f462edfcfeb015d39ca34fbf9a6", "0x1619a5a349f2");
            ADD_CHECKPOINT2(200000, "76d158c401cba51a35d35b5141b7c3bbdca0447431d8714c9797cb30f711e9ff", "0x4021a5da7c7d");
            ADD_CHECKPOINT2(226000, "d4e076d8a4c23e6e51df50ae038f710fe83b1363c69b5d6c94c3d227912ff10c", "0x4a88e4db086d");
            ADD_CHECKPOINT2(263664, "3ea3ebf33bc4c73b00d28addabdf47ca2bf9b0a202f2646a01f5a9121e5d3a54", "0x51e2cbad4ad0");
            ADD_CHECKPOINT2(300000,  "8c5a9f86b20861c1dee6ab90ac86d0b1816163c11f5cf8e23566157e36043998", "0x56c3ddb24cbf");
         ADD_CHECKPOINT2(400000, "5ea6a74691c402be4f428954c00c9b9359a9a1f9afac1317e8115cf793efa039", "0x5cf637763a3e");
         ADD_CHECKPOINT2(500000, "0dbc3dbb1a91236aceef5a7099d5ce07b255648d84738c09bdf9bfd10fa2e44a", "0x601e6e146d7e");
         ADD_CHECKPOINT2(600000, "0a762b3e457ecdc67bd14284aad87844d60f7449366843eca6758c3c82f77c7c", "0x6129272262e0");
         ADD_CHECKPOINT2(650000, "ee01635e35376b62883cf502917cc1b7f4c1343916a50a1a14b20651c465a243", "0x617e04b4f922");
         ADD_CHECKPOINT2(666666, "462b294427f8866bede3ff041b94dc6fac31ea436dc5048f2d1a589b9ff40dce", "0x61a16a2615ce");
         ADD_CHECKPOINT2(700000, "d406c6a4e55fc31fc0f9e26a1afea72125a5232aabb4268ea4cb7bc923cba6ea", "0x61d664bb6692");
         ADD_CHECKPOINT2(777777,   "f9229a8c352d04f32d66314cccdc16eae524db6f76dd0297faf51632de090981", "0x627287c4f700");
         ADD_CHECKPOINT2(835123, "3a0cbe0745f8c9d4c9d01fa047de4a8751b8a18e4629a503bc00945c6f254de4", "0x62c1341ff967");
         ADD_CHECKPOINT2(900000,   "636fbd07ed37e6898f44c90f64d455cbd6313dcb4c0516674f37fc13184ef65c", "0x631872ffbb53");
         ADD_CHECKPOINT2(1000000,  "4f2702ebdd1c8698e4f9eb4ea0a1082845fbad3e5ce3993121594bf66c8bf405", "0x636ac2826f38");
         ADD_CHECKPOINT2(1111111,    "1d6d29f3a27847ec3836a65eb29977e5b8fda03430a27e49f7be8ffb0b81b01c", "0x63b951c05a5b");
         ADD_CHECKPOINT2(1200000,  "0d348de52ab334632f5fd11346728c865fec2444c1c1fd38979d5be8086dcb0f", "0x63f22934ef4c");
         ADD_CHECKPOINT2(1222222,    "ae92b5c9eddbaf8ea2c5a759078781d375f4992f082ae90d1d18bde8e4cda647", "0x63fe57f0061d");
         ADD_CHECKPOINT2(1300000,   "a55a2233e036fecf6c26fdcb593857040b5705718169669d6b870f76249e9201", "0x642788df4168");
         ADD_CHECKPOINT2(1400000,  "38b004a89d0a4573d69c446edd249f6473e20ececa5e9e42d4b5eb44e4b48e16", "0x646e79c1b7cc");
         ADD_CHECKPOINT2(1447206,  "b8d0bea2ab54a1740b14d139709b9c72f7e0911a4342c91b5e9964cfac7f0abe", "0x649192a96ef0");  
         ADD_CHECKPOINT2(1455513,  "38c5ebc4d156f30754fa2a76dbeac1def3f370d4069f997153c50dbdeb7916fb", "0x6496de81ecd7");  
         ADD_CHECKPOINT2(1480000, "e2ad271829a9f9002cc7390140ebc594bd9864f086ee89b66d6ddb8cccf6a54f", "0x1dd86c3cd362");
         ADD_CHECKPOINT2(1500000, "b1557ccca36504b6e3153eef788c4f2b0143ff1ea33b31d141865ffbf495d0ba", "0x1dd86c4f8220"); 
         ADD_CHECKPOINT2(1515219, "c56ee46aa1ab0291217b72bb38ba44080b458a35b1e7d2e78c6d704abd4e8675", "0x1dd8417638f8");
         ADD_CHECKPOINT2(1518738, "9c20ff703df80a169b75f6df54bca323c3754b7b134c9cb9c16b7c3781f457be", "0x1dd8901d0d7b");
         ADD_CHECKPOINT2(1522077, "817bd22b2618d58d48ce02fb709e46658f997dc6f50a6f0bf9f8b2e243b89fdb", "0x1dd841c2b93a");

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
      "seed1.dinastycoin.com",
      "seed2.dinastycoin.com",
      "seed3.dinastycoin.com",
      "seed4.dinastycoin.com"
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

