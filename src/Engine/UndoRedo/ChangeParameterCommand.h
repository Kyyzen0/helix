#pragma once

#include "Engine/Core/Types.h"
#include "Engine/Network/NetworkManager.h"
#include "Engine/Parameter/Parameter.h"
#include "Engine/Serializer/ParameterSerializable.h"
#include "Engine/UndoRedo/UndoCommand.h"
#include <icecream.hpp>
#include <string>

namespace helix::nt {

/// @brief Undo step for a parameter edit. Restores the whole parameter from a
/// snapshot so flat values and multiparm instances both round trip.
class ChangeParameterCommand : public UndoCommand
{
  public:
    ChangeParameterCommand(
        helix::nt::NodeId nodeId,
        std::string paramName,
        ParameterSerializable before,
        ParameterSerializable after
    )
        : nodeId_(nodeId), paramName_(paramName), before_(std::move(before)),
          after_(std::move(after))
    {
    }

    // Flat parameters hand over their value vectors and the snapshot is built here.
    ChangeParameterCommand(
        helix::nt::NodeId nodeId,
        std::string paramName,
        const helix::prm::PrmValues& before,
        const helix::prm::PrmValues& after
    )
        : nodeId_(nodeId), paramName_(paramName), before_(toSerializable(paramName, before)),
          after_(toSerializable(paramName, after))
    {
    }

    void undo() override { restore_(before_); }
    void redo() override { restore_(after_); }

    UndoCommandType type() const override { return UndoCommandType::ChangeParameter; }

  private:
    void restore_(const ParameterSerializable& snapshot)
    {
        IC(nodeId_, paramName_);
        if (!nm().isValidNode(nodeId_))
        {
            IC("ChangeParameterCommand — node not found", nodeId_);
            return;
        }
        if (auto prm = nm().getNode(nodeId_).getParameter(paramName_).lock())
            applySerializable(*prm, snapshot);
        else
            IC("ChangeParameterCommand — parameter not found", nodeId_, paramName_);
    }

    helix::nt::NodeId nodeId_;
    std::string paramName_;
    ParameterSerializable before_;
    ParameterSerializable after_;
};

} // namespace helix::nt
