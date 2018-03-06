#ifndef ENTT_ENTITY_SNAPSHOT_HPP
#define ENTT_ENTITY_SNAPSHOT_HPP


#include <unordered_map>
#include <algorithm>
#include <cstddef>
#include <utility>
#include <cassert>
#include <iterator>
#include <type_traits>
#include "entt_traits.hpp"


namespace entt {


/**
 * @brief Forward declaration of the registry class.
 */
template<typename>
class Registry;


/**
 * @brief Utility class to create snapshots from a registry.
 *
 * A _snapshot_ can be either a dump of the entire registry or a narrower
 * selection of components and tags of interest.<br/>
 * This type can be used in both cases if provided with a correctly configured
 * output archive.
 *
 * @tparam Entity A valid entity type (see entt_traits for more details).
 */
template<typename Entity>
class Snapshot final {
    /*! @brief A registry is allowed to create snapshots. */
    friend class Registry<Entity>;

    using func_type = Entity(*)(Registry<Entity> &, Entity);

    Snapshot(Registry<Entity> &registry, Entity seed, std::size_t size, func_type follow) noexcept
        : registry{registry},
          seed{seed},
          size{size},
          follow{follow}
    {}

    Snapshot(const Snapshot &) = default;
    Snapshot(Snapshot &&) = default;

    Snapshot & operator=(const Snapshot &) = default;
    Snapshot & operator=(Snapshot &&) = default;

    template<typename View, typename Archive>
    void get(Archive &archive, const View &view) {
        archive(static_cast<Entity>(view.size()));

        for(typename View::size_type i{}, sz = view.size(); i < sz; ++i) {
            archive(view.data()[i]);
            archive(view.raw()[i]);
        };
    }

    template<typename Tag, typename Archive>
    void get(Archive &archive) {
        const bool has = registry.template has<Tag>();

        // numerical length is forced for tags to facilitate loading
        archive(has ? Entity(1): Entity{});

        if(has) {
            archive(registry.template attachee<Tag>());
            archive(registry.template get<Tag>());
        }
    }

public:
    /**
     * @brief Puts aside all the entities that are still in use.
     *
     * Entities are serialized along with their versions. Destroyed entities are
     * not taken in consideration by this function.
     *
     * @tparam Archive Type of output archive.
     * @param archive A valid reference to an output archive.
     * @return An object of this type to continue creating the snapshot.
     */
    template<typename Archive>
    Snapshot entities(Archive &archive) && {
        archive(static_cast<Entity>(registry.size()));
        registry.each([&archive, this](auto entity) { archive(entity); });
        return *this;
    }

    /**
     * @brief Puts aside destroyed entities.
     *
     * Entities are serialized along with their versions. Entities that are
     * still in use are not taken in consideration by this function.
     *
     * @tparam Archive Type of output archive.
     * @param archive A valid reference to an output archive.
     * @return An object of this type to continue creating the snapshot.
     */
    template<typename Archive>
    Snapshot destroyed(Archive &archive) && {
        archive(static_cast<Entity>(size));

        if(size) {
            auto curr = seed;
            archive(curr);

            for(auto i = size - 1; i; --i) {
                curr = follow(registry, curr);
                archive(curr);
            }
        }

        return *this;
    }

    /**
     * @brief Puts aside the given components.
     *
     * Each component is serialized together with the entity to which it
     * belongs. Entities are serialized along with their versions.
     *
     * @tparam Component Types of components to serialize.
     * @tparam Archive Type of output archive.
     * @param archive A valid reference to an output archive.
     * @return An object of this type to continue creating the snapshot.
     */
    template<typename... Component, typename Archive>
    Snapshot component(Archive &archive) && {
        using accumulator_type = int[];
        accumulator_type accumulator = { 0, (get(archive, registry.template view<Component>()), 0)... };
        (void)accumulator;
        return *this;
    }

    /**
     * @brief Puts aside the given tags.
     *
     * Each tag is serialized together with the entity to which it belongs.
     * Entities are serialized along with their versions.
     *
     * @tparam Tag Types of tags to serialize.
     * @tparam Archive Type of output archive.
     * @param archive A valid reference to an output archive.
     * @return An object of this type to continue creating the snapshot.
     */
    template<typename... Tag, typename Archive>
    Snapshot tag(Archive &archive) && {
        using accumulator_type = int[];
        accumulator_type accumulator = { 0, (get<Tag>(archive), 0)... };
        (void)accumulator;
        return *this;
    }

private:
    Registry<Entity> &registry;
    const Entity seed;
    const std::size_t size;
    func_type follow;
};


/**
 * @brief Utility class to restore a snapshot as a whole.
 *
 * A snapshot loader requires that the destination registry be empty and loads
 * all the data at once while keeping intact the identifiers that the entities
 * originally had.<br/>
 * An example of use is the implementation of a save/restore utility.
 *
 * @tparam Entity A valid entity type (see entt_traits for more details).
 */
template<typename Entity>
class SnapshotLoader final {
    /*! @brief A registry is allowed to create snapshot loaders. */
    friend class Registry<Entity>;

    using func_type = void(*)(Registry<Entity> &, Entity, bool);

    SnapshotLoader(Registry<Entity> &registry, func_type ensure_fn) noexcept
        : registry{registry},
          ensure_fn{ensure_fn}
    {
        // restore a snapshot as a whole requires a clean registry
        assert(!registry.capacity());
    }

    SnapshotLoader(const SnapshotLoader &) = default;
    SnapshotLoader(SnapshotLoader &&) = default;

    SnapshotLoader & operator=(const SnapshotLoader &) = default;
    SnapshotLoader & operator=(SnapshotLoader &&) = default;

    template<typename Archive, typename Func>
    void each(Archive &archive, Func func) {
        Entity length{};
        archive(length);

        while(length) {
            Entity entity{};
            archive(entity);
            func(entity);
            --length;
        }
    }

    template<typename Component, typename Archive>
    void assign(Archive &archive) {
        each(archive, [&archive, this](auto entity) {
            static constexpr auto destroyed = false;
            ensure_fn(registry, entity, destroyed);
            archive(registry.template assign<Component>(entity));
        });
    }

    template<typename Tag, typename Archive>
    void attach(Archive &archive) {
        each(archive, [&archive, this](auto entity) {
            static constexpr auto destroyed = false;
            ensure_fn(registry, entity, destroyed);
            archive(registry.template attach<Tag>(entity));
        });
    }

public:
    /**
     * @brief Restores entities that were in use during serialization.
     *
     * This function restores the entities that were in use during serialization
     * and gives them the versions they originally had.
     *
     * @tparam Archive Type of input archive.
     * @param archive A valid reference to an input archive.
     * @return A valid loader to continue restoring data.
     */
    template<typename Archive>
    SnapshotLoader entities(Archive &archive) && {
        each(archive, [this](auto entity) {
            static constexpr auto destroyed = false;
            ensure_fn(registry, entity, destroyed);
        });

        return *this;
    }

    /**
     * @brief Restores entities that were destroyed during serialization.
     *
     * This function restores the entities that were destroyed during
     * serialization and gives them the versions they originally had.
     *
     * @tparam Archive Type of input archive.
     * @param archive A valid reference to an input archive.
     * @return A valid loader to continue restoring data.
     */
    template<typename Archive>
    SnapshotLoader destroyed(Archive &archive) && {
        each(archive, [this](auto entity) {
            static constexpr auto destroyed = true;
            ensure_fn(registry, entity, destroyed);
        });

        return *this;
    }

    /**
     * @brief Restores components and assigns them to the right entities.
     *
     * The template parameter list must be exactly the same used during
     * serialization. In the event that the entity to which the component is
     * assigned doesn't exist yet, the loader will take care to create it with
     * the version it originally had.
     *
     * @tparam Component Types of components to restore.
     * @tparam Archive Type of input archive.
     * @param archive A valid reference to an input archive.
     * @return A valid loader to continue restoring data.
     */
    template<typename... Component, typename Archive>
    SnapshotLoader component(Archive &archive) && {
        using accumulator_type = int[];
        accumulator_type accumulator = { 0, (assign<Component>(archive), 0)... };
        (void)accumulator;
        return *this;
    }

    /**
     * @brief Restores tags and assigns them to the right entities.
     *
     * The template parameter list must be exactly the same used during
     * serialization. In the event that the entity to which the tag is assigned
     * doesn't exist yet, the loader will take care to create it with the
     * version it originally had.
     *
     * @tparam Tag Types of tags to restore.
     * @tparam Archive Type of input archive.
     * @param archive A valid reference to an input archive.
     * @return A valid loader to continue restoring data.
     */
    template<typename... Tag, typename Archive>
    SnapshotLoader tag(Archive &archive) && {
        using accumulator_type = int[];
        accumulator_type accumulator = { 0, (attach<Tag>(archive), 0)... };
        (void)accumulator;
        return *this;
    }


    /**
     * @brief Destroys those entities that have neither components nor tags.
     *
     * In case all the entities were serialized but only part of the components
     * and tags was saved, it could happen that some of the entities have
     * neither components nor tags once restored.<br/>
     * This functions helps to identify and destroy those entities.
     *
     * @return A valid loader to continue restoring data.
     */
    SnapshotLoader orphans() && {
        registry.orphans([this](auto entity) {
            registry.destroy(entity);
        });

        return *this;
    }

private:
    Registry<Entity> &registry;
    func_type ensure_fn;
};


/**
 * @brief Utility class for _continuous loading_.
 *
 * A _continuous loader_ is designed to load data from a source registry to a
 * (possibly) non-empty destination. The loader can accomodate in a registry
 * more than one snapshot in a sort of _continuous loading_ that updates the
 * destination one step at a time.<br/>
 * Identifiers that entities originally had are not transferred to the target.
 * Instead, the loader maps remote identifiers to local ones while restoring a
 * snapshot.<br/>
 * An example of use is the implementation of a client-server applications with
 * the requirement of transferring somehow parts of the representation side to
 * side.
 *
 * @tparam Entity A valid entity type (see entt_traits for more details).
 */
template<typename Entity>
class ContinuousLoader final {
    using traits_type = entt_traits<Entity>;

    Entity destroy(Entity entity) {
        const auto it = remloc.find(entity);

        if(it == remloc.cend()) {
            const auto local = registry.create();
            remloc.emplace(entity, std::make_pair(local, true));
            registry.destroy(local);
        }

        return remloc[entity].first;
    }

    Entity restore(Entity entity) {
        const auto it = remloc.find(entity);

        if(it == remloc.cend()) {
            const auto local = registry.create();
            remloc.emplace(entity, std::make_pair(local, true));
        } else {
            remloc[entity].first =
                    registry.valid(remloc[entity].first)
                    ? remloc[entity].first
                    : registry.create();

            // set the dirty flag
            remloc[entity].second = true;
        }

        return remloc[entity].first;
    }

    template<typename Instance, typename Type>
    std::enable_if_t<std::is_same<Type, Entity>::value>
    update(Instance &instance, Type Instance::*member) {
        instance.*member = map(instance.*member);
    }

    template<typename Instance, typename Type>
    std::enable_if_t<std::is_same<typename std::iterator_traits<typename Type::iterator>::value_type, Entity>::value>
    update(Instance &instance, Type Instance::*member) {
        for(auto &entity: (instance.*member)) {
            entity = map(entity);
        }
    }

    template<typename Archive, typename Func>
    void each(Archive &archive, Func func) {
        Entity length{};
        archive(length);

        while(length) {
            Entity entity{};
            archive(entity);
            func(entity);
            --length;
        }
    }

    template<typename Component>
    void reset() {
        for(auto &&ref: remloc) {
            const auto local = ref.second.first;

            if(registry.valid(local)) {
                registry.template reset<Component>(local);
            }
        }
    }

    template<typename Component, typename Archive>
    void assign(Archive &archive) {
        reset<Component>();

        each(archive, [&archive, this](auto entity) {
            entity = restore(entity);
            archive(registry.template accomodate<Component>(entity));
        });
    }

    template<typename Component, typename Archive, typename... Type>
    void assign(Archive &archive, Type Component::*... member) {
        reset<Component>();

        each(archive, [&archive, member..., this](auto entity) {
            entity = restore(entity);
            auto &component = registry.template accomodate<Component>(entity);
            archive(component);

            using accumulator_type = int[];
            accumulator_type accumulator = { 0, (update(component, member), 0)... };
            (void)accumulator;
        });
    }

    template<typename Tag, typename Archive>
    void attach(Archive &archive) {
        registry.template remove<Tag>();

        each(archive, [&archive, this](auto entity) {
            entity = restore(entity);
            archive(registry.template attach<Tag>(entity));
        });
    }

    template<typename Tag, typename Archive, typename... Type>
    void attach(Archive &archive, Type Tag::*... member) {
        registry.template remove<Tag>();

        each(archive, [&archive, member..., this](auto entity) {
            entity = restore(entity);
            auto &tag = registry.template attach<Tag>(entity);
            archive(tag);

            using accumulator_type = int[];
            accumulator_type accumulator = { 0, (update(tag, member), 0)... };
            (void)accumulator;
        });
    }

public:
    /*! @brief Underlying entity identifier. */
    using entity_type = Entity;

    /**
     * @brief Constructs a loader that is bound to a given registry.
     * @param registry A valid reference to a registry.
     */
    ContinuousLoader(Registry<entity_type> &registry) noexcept
        : registry{registry}
    {}

    /*! @brief Default copy constructor. */
    ContinuousLoader(const ContinuousLoader &) = default;
    /*! @brief Default move constructor. */
    ContinuousLoader(ContinuousLoader &&) = default;

    /*! @brief Default copy assignment operator. @return This loader. */
    ContinuousLoader & operator=(const ContinuousLoader &) = default;
    /*! @brief Default move assignment operator. @return This loader. */
    ContinuousLoader & operator=(ContinuousLoader &&) = default;

    /**
     * @brief Restores entities that were in use during serialization.
     *
     * This function restores the entities that were in use during serialization
     * and creates local counterparts for them if required.
     *
     * @tparam Archive Type of input archive.
     * @param archive A valid reference to an input archive.
     * @return A non-const reference to this loader.
     */
    template<typename Archive>
    ContinuousLoader & entities(Archive &archive) {
        each(archive, [this](auto entity) { restore(entity); });
        return *this;
    }

    /**
     * @brief Restores entities that were destroyed during serialization.
     *
     * This function restores the entities that were destroyed during
     * serialization and creates local counterparts for them if required.
     *
     * @tparam Archive Type of input archive.
     * @param archive A valid reference to an input archive.
     * @return A non-const reference to this loader.
     */
    template<typename Archive>
    ContinuousLoader & destroyed(Archive &archive) {
        each(archive, [this](auto entity) { destroy(entity); });
        return *this;
    }

    /**
     * @brief Restores components and assigns them to the right entities.
     *
     * The template parameter list must be exactly the same used during
     * serialization. In the event that the entity to which the component is
     * assigned doesn't exist yet, the loader will take care to create a local
     * counterpart for it.
     *
     * @tparam Component Types of components to restore.
     * @tparam Archive Type of input archive.
     * @param archive A valid reference to an input archive.
     * @return A non-const reference to this loader.
     */
    template<typename... Component, typename Archive>
    ContinuousLoader & component(Archive &archive) {
        using accumulator_type = int[];
        accumulator_type accumulator = { 0, (assign<Component>(archive), 0)... };
        (void)accumulator;
        return *this;
    }

    /**
     * @brief Restores components and assigns them to the right entities.
     *
     * The template parameter list must be exactly the same used during
     * serialization. In the event that the entity to which the component is
     * assigned doesn't exist yet, the loader will take care to create a local
     * counterpart for it.<br/>
     * Members can be either data members of type entity_type or containers of
     * entities. In both cases, the loader will visit them and update the
     * entities by replacing each one with its local counterpart.
     *
     * @tparam Component Type of component to restore.
     * @tparam Archive Type of input archive.
     * @tparam Type Types of members to update with their local counterparts.
     * @param archive A valid reference to an input archive.
     * @param member Members to update with their local counterparts.
     * @return A non-const reference to this loader.
     */
    template<typename Component, typename Archive, typename... Type>
    ContinuousLoader & component(Archive &archive, Type Component::*... member) {
        assign(archive, member...);
        return *this;
    }

    /**
     * @brief Restores tags and assigns them to the right entities.
     *
     * The template parameter list must be exactly the same used during
     * serialization. In the event that the entity to which the tag is assigned
     * doesn't exist yet, the loader will take care to create a local
     * counterpart for it.
     *
     * @tparam Tag Types of tags to restore.
     * @tparam Archive Type of input archive.
     * @param archive A valid reference to an input archive.
     * @return A non-const reference to this loader.
     */
    template<typename... Tag, typename Archive>
    ContinuousLoader & tag(Archive &archive) {
        using accumulator_type = int[];
        accumulator_type accumulator = { 0, (attach<Tag>(archive), 0)... };
        (void)accumulator;
        return *this;
    }

    /**
     * @brief Restores tags and assigns them to the right entities.
     *
     * The template parameter list must be exactly the same used during
     * serialization. In the event that the entity to which the tag is assigned
     * doesn't exist yet, the loader will take care to create a local
     * counterpart for it.<br/>
     * Members can be either data members of type entity_type or containers of
     * entities. In both cases, the loader will visit them and update the
     * entities by replacing each one with its local counterpart.
     *
     * @tparam Tag Type of tag to restore.
     * @tparam Archive Type of input archive.
     * @tparam Type Types of members to update with their local counterparts.
     * @param archive A valid reference to an input archive.
     * @param member Members to update with their local counterparts.
     * @return A non-const reference to this loader.
     */
    template<typename Tag, typename Archive, typename... Type>
    ContinuousLoader & tag(Archive &archive, Type Tag::*... member) {
        attach<Tag>(archive, member...);
        return *this;
    }

    /**
     * @brief Helps to purge entities that no longer have a conterpart.
     *
     * Users should invoke this member function after restoring each snapshot,
     * unless they know exactly what they are doing.
     *
     * @return A non-const reference to this loader.
     */
    ContinuousLoader & shrink() {
        auto it = remloc.begin();

        while(it != remloc.cend()) {
            const auto local = it->second.first;
            bool &dirty = it->second.second;

            if(dirty) {
                dirty = false;
                ++it;
            } else {
                if(registry.valid(local)) {
                    registry.destroy(local);
                }

                it = remloc.erase(it);
            }
        }

        return *this;
    }

    /**
     * @brief Destroys those entities that have neither components nor tags.
     *
     * In case all the entities were serialized but only part of the components
     * and tags was saved, it could happen that some of the entities have
     * neither components nor tags once restored.<br/>
     * This functions helps to identify and destroy those entities.
     *
     * @return A non-const reference to this loader.
     */
    ContinuousLoader & orphans() {
        registry.orphans([this](auto entity) {
            registry.destroy(entity);
        });

        return *this;
    }

    /**
     * @brief Tests if a loader knows about a given entity.
     * @param entity An entity identifier.
     * @return True if `entity` is managed by the loader, false otherwise.
     */
    bool has(entity_type entity) {
        return !(remloc.find(entity) == remloc.cend());
    }

    /**
     * @brief Returns the identifier to which an entity refers.
     *
     * @warning
     * Attempting to use an entity that isn't managed by the loader results in
     * undefined behavior.<br/>
     * An assertion will abort the execution at runtime in debug mode if the
     * loader doesn't knows about the entity.
     *
     * @param entity An entity identifier.
     * @return The identifier to which `entity` refers in the target registry.
     */
    entity_type map(entity_type entity) {
        assert(has(entity));
        return remloc[entity].first;
    }

private:
    std::unordered_map<Entity, std::pair<Entity, bool>> remloc;
    Registry<Entity> &registry;
};


}


#endif // ENTT_ENTITY_SNAPSHOT_HPP
