#pragma once

#include "Engine/Core/Types.h"
namespace helix::prm {
class Name
{
  public:
    Name(String token, String label);
    Name(String token);
    Name();

    const String& getToken() const;
    const String& getLabel() const;

  private:
    String token_;
    String label_;
};

} // namespace helix::prm
