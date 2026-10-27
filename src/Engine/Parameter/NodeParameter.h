#pragma once
#include "Engine/Core/Types.h"
#include "Engine/Parameter/Parameter.h"

namespace helix::prm {

class NodeParameter : public Parameter
{
  public:
    NodeParameter(Template prmTemplate, helix::nt::NodeId nodeId);
    helix::nt::NodeId getNodeId() const { return nodeId_; }

  protected:
    /// @brief Builds the context an expression runs from, e.g. what node the
    /// parameter belongs to.
    std::unique_ptr<expr::ExpressionContext> makeExpressionContext_() const override;

    /// @brief Records the parameters an expression read as captured dependencies
    /// of this component in the network graph.
    void submitExpressionDependencies_(
        const expr::ExpressionContext& context,
        unsigned int index
    ) const override;

  private:
    helix::nt::NodeId nodeId_;
};
} // namespace helix::prm
