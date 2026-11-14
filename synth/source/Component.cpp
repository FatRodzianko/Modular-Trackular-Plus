#include "Component.h"

#include <algorithm>
#include <cassert>

namespace polysynth
{

Component::~Component() // NOLINT(bugprone-exception-escape) unit test only exception
{
    if (registry != nullptr)
    {
        registry->unregisterComponent(*this);
    }
}

void ComponentRegistry::registerComponent(Component& component)
{
    components.push_back(&component);
    component.setRegistry(shared_from_this());
}

void ComponentRegistry::unregisterComponent(Component& component)
{
#ifdef UNIT_TESTS
    // exception to catch in a test
    if (std::ranges::find(components, &component) == std::end(components))
    {
        throw std::runtime_error("synth component not registered");
    }
#else
    assert(std::ranges::find(components, &component) != std::end(components));
#endif
    std::erase(components, &component);
    component.setRegistry(nullptr);
}

}
