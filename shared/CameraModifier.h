#pragma once
#include "CamPath.h"
#include <cstdint>
#include <string>

// Shared settings; camera links own order, influence and enabled state.
struct MirvCameraModifier
{
 std::uint64_t Id=0;
 std::string Name;
 CamPath::Modifier Settings;
};
