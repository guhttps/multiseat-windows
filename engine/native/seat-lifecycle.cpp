#include "seat-lifecycle.h"

namespace multiseat::native {

std::array<ResourceKey, 3> SeatResources::Keys() const noexcept {
    return {monitor, keyboard, mouse};
}

bool ValidResources(const SeatResources& resources) noexcept {
    if (resources.monitor.kind != ResourceKind::Monitor ||
        resources.keyboard.kind != ResourceKind::Keyboard ||
        resources.mouse.kind != ResourceKind::Mouse) return false;
    for (const auto& key : resources.Keys())
        if (key.id == 0 || key.arrival_generation == 0) return false;
    return true;
}

bool SameEndpoint(const ResourceKey& left, const ResourceKey& right) noexcept {
    return left.kind == right.kind && left.id == right.id;
}

template class BasicSeatLifecycle<VerifiedIdentity>;

} // namespace multiseat::native
