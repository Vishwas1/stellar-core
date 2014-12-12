#include "generated/stellar.hh"

namespace stellar
{
    class Config
    {

    public:
        // application config
        bool RUN_STANDALONE;
        int PROTOCOL_VERSION;
        std::string VERSION_STR;
        std::string LOG_FILE_PATH;
        uint32_t DESIRED_BASE_FEE;  // in stroops

        // overlay config
        int PEER_PORT;
        unsigned TARGET_PEER_CONNECTIONS;
        unsigned MAX_PEER_CONNECTIONS;
        // Peers we will always try to stay connected to
        std::vector<std::string> PREFERRED_PEERS;
        std::vector<std::string> KNOWN_PEERS;


        // FBA config
        stellarxdr::uint256 VALIDATION_SEED;
        int QUORUM_THRESHOLD;
        std::vector<stellarxdr::uint256> QUORUM_SET;

        Config();

        void load(std::string const& filename);
    };
}