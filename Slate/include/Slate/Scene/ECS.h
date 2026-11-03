#pragma once

#include "Slate/Core/Base.h"
#include "Slate/Core/Log.h"

#include <typeindex>
#include <unordered_map>
#include <vector>
#include <algorithm>

namespace Slate {

using EntityID = uint32_t;
constexpr EntityID NullEntity = 0;

class Registry;

// A lightweight handle to an entity. Entity handles are validated on access:
// destroying an entity invalidates every handle that refers to it.
class Entity
{
public:
    Entity() = default;
    Entity(EntityID id, uint32_t generation, Registry* registry)
        : m_ID(id), m_Generation(generation), m_Registry(registry) {}

    EntityID GetID() const { return m_ID; }
    uint32_t GetGeneration() const { return m_Generation; }
    Registry* GetRegistry() const { return m_Registry; }

    explicit operator bool() const { return IsValid(); }
    bool operator==(const Entity& other) const
    {
        return m_ID == other.m_ID && m_Generation == other.m_Generation && m_Registry == other.m_Registry;
    }
    bool operator!=(const Entity& other) const { return !(*this == other); }

    bool IsValid() const;

    template<typename T> bool HasComponent() const;
    template<typename T> T& GetComponent() const;
    template<typename T> T* TryGetComponent() const;
    template<typename T, typename... Args> T& AddComponent(Args&&... args) const;
    template<typename T> void RemoveComponent() const;

private:
    EntityID m_ID = NullEntity;
    uint32_t m_Generation = 0;
    Registry* m_Registry = nullptr;
};

// Sparse-set component storage. Iteration is dense; lookups are O(1) through a
// hash map keyed on the entity index.
class IComponentPool
{
public:
    virtual ~IComponentPool() = default;
    virtual void Remove(EntityID entity) = 0;
    virtual bool Contains(EntityID entity) const = 0;
    virtual size_t Size() const = 0;
    virtual const std::vector<EntityID>& GetEntities() const = 0;
};

template<typename T>
class ComponentPool final : public IComponentPool
{
public:
    T& Add(EntityID entity, T component)
    {
        SLATE_ASSERT(m_Lookup.find(entity) == m_Lookup.end(), "Component already exists on entity {}", entity);
        m_Lookup[entity] = m_Components.size();
        m_Entities.push_back(entity);
        m_Components.push_back(std::move(component));
        return m_Components.back();
    }

    void Remove(EntityID entity) override
    {
        const auto it = m_Lookup.find(entity);
        if (it == m_Lookup.end())
            return;

        const size_t index = it->second;
        const size_t last = m_Components.size() - 1;

        if (index != last)
        {
            m_Components[index] = std::move(m_Components[last]);
            m_Entities[index] = m_Entities[last];
            m_Lookup[m_Entities[index]] = index;
        }

        m_Components.pop_back();
        m_Entities.pop_back();
        m_Lookup.erase(it);
    }

    bool Contains(EntityID entity) const override { return m_Lookup.find(entity) != m_Lookup.end(); }
    size_t Size() const override { return m_Components.size(); }
    const std::vector<EntityID>& GetEntities() const override { return m_Entities; }

    T& Get(EntityID entity)
    {
        const auto it = m_Lookup.find(entity);
        SLATE_ASSERT(it != m_Lookup.end(), "Component missing on entity {}", entity);
        return m_Components[it->second];
    }

    T* TryGet(EntityID entity)
    {
        const auto it = m_Lookup.find(entity);
        return it == m_Lookup.end() ? nullptr : &m_Components[it->second];
    }

private:
    std::vector<T> m_Components;
    std::vector<EntityID> m_Entities;
    std::unordered_map<EntityID, size_t> m_Lookup;
};

// Owns entity bookkeeping and all component pools.
class Registry
{
public:
    Registry()
    {
        // Entity id 0 is reserved as the null handle.
        m_Generations.push_back(0);
        m_Alive.push_back(false);
    }
    ~Registry() = default;

    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;

    Entity CreateEntity()
    {
        EntityID id = NullEntity;
        uint32_t generation = 0;

        if (!m_FreeList.empty())
        {
            id = m_FreeList.back();
            m_FreeList.pop_back();
            generation = m_Generations[id];
        }
        else
        {
            id = static_cast<EntityID>(m_Generations.size());
            m_Generations.push_back(0);
            m_Alive.push_back(false);
        }

        SLATE_ASSERT(id != NullEntity, "Entity id 0 is reserved");
        m_Alive[id] = true;
        return Entity(id, generation, this);
    }

    void DestroyEntity(Entity entity)
    {
        if (!entity.IsValid())
            return;

        const EntityID id = entity.GetID();
        for (auto& [type, pool] : m_Pools)
            pool->Remove(id);

        m_Alive[id] = false;
        ++m_Generations[id];
        m_FreeList.push_back(id);
    }

    bool IsAlive(EntityID id) const
    {
        return id < m_Alive.size() && m_Alive[id];
    }

    uint32_t GetGeneration(EntityID id) const
    {
        return id < m_Generations.size() ? m_Generations[id] : 0;
    }

    size_t GetEntityCount() const
    {
        size_t count = 0;
        for (bool alive : m_Alive)
            if (alive)
                ++count;
        return count;
    }

    // Removes every entity and component. Used when loading a scene.
    void Clear()
    {
        m_Pools.clear();
        m_Generations.clear();
        m_Alive.clear();
        m_FreeList.clear();
        m_Generations.push_back(0);
        m_Alive.push_back(false);
    }

    template<typename T>
    ComponentPool<T>& GetPool()
    {
        const std::type_index type(typeid(T));
        auto it = m_Pools.find(type);
        if (it == m_Pools.end())
        {
            auto pool = MakeScope<ComponentPool<T>>();
            ComponentPool<T>* raw = pool.get();
            m_Pools.emplace(type, std::move(pool));
            return *raw;
        }
        return *static_cast<ComponentPool<T>*>(it->second.get());
    }

    template<typename T>
    ComponentPool<T>* TryGetPool()
    {
        const std::type_index type(typeid(T));
        auto it = m_Pools.find(type);
        if (it == m_Pools.end())
            return nullptr;
        return static_cast<ComponentPool<T>*>(it->second.get());
    }

    // Iterates every entity that has all of the requested components. The
    // iteration order follows the smallest pool for efficiency.
    template<typename... T, typename F>
    void Each(F&& function)
    {
        static_assert(sizeof...(T) > 0, "Each requires at least one component type");

        IComponentPool* smallest = nullptr;
        size_t smallestSize = SIZE_MAX;

        auto consider = [&smallest, &smallestSize](IComponentPool* pool, size_t size)
        {
            if (size < smallestSize)
            {
                smallest = pool;
                smallestSize = size;
            }
        };

        (consider(&GetPool<T>(), GetPool<T>().Size()), ...);

        if (!smallest || smallestSize == 0)
            return;

        // Copy the entity list because the callback may create or destroy
        // entities, invalidating the pool's dense array.
        const std::vector<EntityID> entities = smallest->GetEntities();

        for (EntityID id : entities)
        {
            if (!IsAlive(id))
                continue;
            if (!(GetPool<T>().Contains(id) && ...))
                continue;

            Entity entity(id, m_Generations[id], this);
            function(entity, GetPool<T>().Get(id)...);
        }
    }

private:
    std::vector<uint32_t> m_Generations;
    std::vector<bool> m_Alive;
    std::vector<EntityID> m_FreeList;
    std::unordered_map<std::type_index, Scope<IComponentPool>> m_Pools;
};

// ---------------------------------------------------------------------------
// Entity template implementations
// ---------------------------------------------------------------------------

inline bool Entity::IsValid() const
{
    return m_Registry != nullptr && m_Registry->IsAlive(m_ID) && m_Registry->GetGeneration(m_ID) == m_Generation;
}

template<typename T>
bool Entity::HasComponent() const
{
    if (!IsValid())
        return false;
    return m_Registry->GetPool<T>().Contains(m_ID);
}

template<typename T>
T& Entity::GetComponent() const
{
    SLATE_ASSERT(IsValid(), "Entity handle is invalid");
    return m_Registry->GetPool<T>().Get(m_ID);
}

template<typename T>
T* Entity::TryGetComponent() const
{
    if (!IsValid())
        return nullptr;
    return m_Registry->GetPool<T>().TryGet(m_ID);
}

template<typename T, typename... Args>
T& Entity::AddComponent(Args&&... args) const
{
    SLATE_ASSERT(IsValid(), "Entity handle is invalid");
    return m_Registry->GetPool<T>().Add(m_ID, T{ std::forward<Args>(args)... });
}

template<typename T>
void Entity::RemoveComponent() const
{
    if (!IsValid())
        return;
    m_Registry->GetPool<T>().Remove(m_ID);
}

} // namespace Slate
