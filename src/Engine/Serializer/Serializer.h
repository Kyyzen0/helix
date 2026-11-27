#include "Engine/Network/NetworkManager.h"
#include <iosfwd>
#include <string>

namespace helix::nt {

class Serializer
{
  public:
    /// @brief Writes a network out to a file.
    void save(NetworkManager& networkManager, std::string filePath);

    /// @brief Reads a file into the network, replacing whatever was there.
    /// @note Loading creates nodes, which runs through undo, cooking, and the signals the
    /// interface listens to, so it goes through the manager rather than the network alone.
    void load(NetworkManager& networkManager, std::string filePath);

    /// @brief Writes a network out to a stream, in the format of a file.
    /// @note The archive closes its brackets when it is done, so the stream holds a whole
    /// scene when this returns. Throws when the stream refuses the text.
    void save(NetworkManager& networkManager, std::ostream& stream);

    /// @brief Reads a stream into the network, replacing whatever was there.
    /// @note The network is cleared before the stream is read, so one that cannot be read
    /// leaves the scene empty. Throws on text that is not a scene.
    void load(NetworkManager& networkManager, std::istream& stream);
};

} // namespace helix::nt
