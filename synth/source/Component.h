#pragma once

#include <memory>
#include <vector>

namespace polysynth
{
class ComponentRegistry;

/**
 * Component of a synth instance that can handle updates in audio and main thread.
 */
class Component
{
  public:
    virtual ~Component(); // NOLINT(bugprone-exception-escape) unit test only exception
    Component() = default;
    Component(const Component&) = delete;
    Component& operator=(const Component&) = delete;
    Component(Component&&) = delete;
    Component& operator=(Component&&) = delete;

    virtual void updateAudio(){};
    virtual void updateMain(){};

  private:
    std::shared_ptr<ComponentRegistry> registry = nullptr;

    void setRegistry(std::shared_ptr<ComponentRegistry> registry)
    {
        this->registry = std::move(registry);
    }

    friend class ComponentRegistry;
};

/**
 * Registry (container) of components.
 */
class ComponentRegistry : public std::enable_shared_from_this<ComponentRegistry>
{
  public:
    void registerComponent(Component& component);
    void unregisterComponent(Component& component);

    const std::vector<Component*>& getComponents() const { return components; };

  private:
    /**
     * Using raw pointer for convenience of not forcing API client to use shared_ptr.
     * Lifecycle is safe, unregister called from SynthComponent destructor.
     */
    std::vector<Component*> components;
};
}
