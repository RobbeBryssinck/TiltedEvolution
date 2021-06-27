#include <Services/InventoryService.h>

#include <Messages/RequestObjectInventoryChanges.h>
#include <Messages/NotifyObjectInventoryChanges.h>
#include <Messages/RequestCharacterInventoryChanges.h>
#include <Messages/NotifyCharacterInventoryChanges.h>

#include <Events/UpdateEvent.h>
#include <Events/InventoryChangeEvent.h>

#include <World.h>
#include <Games/Misc/UI.h>
#include <PlayerCharacter.h>
#include <Forms/TESObjectCELL.h>
#include <Actor.h>
#include <Structs/ObjectData.h>
#include <Forms/TESWorldSpace.h>
#include <Games/TES.h>

InventoryService::InventoryService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_transport(aTransport)
{
    m_updateConnection = m_dispatcher.sink<UpdateEvent>().connect<&InventoryService::OnUpdate>(this);
    m_inventoryConnection = m_dispatcher.sink<InventoryChangeEvent>().connect<&InventoryService::OnInventoryChangeEvent>(this);
    m_objectInventoryChangeConnection = m_dispatcher.sink<NotifyObjectInventoryChanges>().connect<&InventoryService::OnObjectInventoryChanges>(this);
    m_characterInventoryChangeConnection = m_dispatcher.sink<NotifyCharacterInventoryChanges>().connect<&InventoryService::OnCharacterInventoryChanges>(this);
#if TP_SKYRIM64
    EventDispatcherManager::Get()->containerChangedEvent.RegisterSink(this);
#endif
}

void InventoryService::OnUpdate(const UpdateEvent& acUpdateEvent) noexcept
{
    RunObjectInventoryUpdates();
    RunCharacterInventoryUpdates();

    ApplyCachedObjectInventoryChanges();
    ApplyCachedCharacterInventoryChanges();
}

#if TP_SKYRIM64
BSTEventResult InventoryService::OnEvent(const TESContainerChangedEvent* acEvent, const EventDispatcher<TESContainerChangedEvent>* aDispatcher)
{
    spdlog::warn("Old: {:X}, new: {:X}", acEvent->oldContainerID, acEvent->newContainerID);

    if (acEvent->newContainerID)
        m_world.GetRunner().Trigger(InventoryChangeEvent(acEvent->newContainerID));
    if (acEvent->oldContainerID)
        m_world.GetRunner().Trigger(InventoryChangeEvent(acEvent->oldContainerID));

    return BSTEventResult::kOk;
}
#endif

void InventoryService::OnInventoryChangeEvent(const InventoryChangeEvent& acEvent) noexcept
{
    const auto* pForm = TESForm::GetById(acEvent.FormId);
    if (RTTI_CAST(pForm, TESForm, Actor))
    {
        spdlog::error("Inventory change added to characters");
        m_charactersWithInventoryChanges.insert(acEvent.FormId);
    }
    else
    {
        spdlog::critical("Inventory change added to objects");
        m_objectsWithInventoryChanges.insert(acEvent.FormId);
    }
}

void InventoryService::OnObjectInventoryChanges(const NotifyObjectInventoryChanges& acMessage) noexcept
{
    spdlog::info("OnObjectInventoryChanges");

    for (const auto& [id, inventory] : acMessage.Changes)
    {
        m_cachedObjectInventoryChanges[id] = inventory;
    }

    ApplyCachedObjectInventoryChanges();
}

void InventoryService::OnCharacterInventoryChanges(const NotifyCharacterInventoryChanges& acMessage) noexcept
{
    spdlog::warn("OnCharacterInventoryChanges");

    for (const auto& [id, inventory] : acMessage.Changes)
    {
        m_cachedCharacterInventoryChanges[id] = inventory;
    }

    ApplyCachedCharacterInventoryChanges();
}

void InventoryService::RunObjectInventoryUpdates() noexcept
{
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenSnapshots = 250ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenSnapshots)
        return;

    lastSendTimePoint = now;

    if (!m_objectsWithInventoryChanges.empty())
    {
        RequestObjectInventoryChanges message;

        for (const auto objectId : m_objectsWithInventoryChanges)
        {
            const auto* pObject = RTTI_CAST(TESForm::GetById(objectId), TESForm, TESObjectREFR);

            if (!pObject)
                continue;

            ObjectData objectData;

            GameId gameId(0, 0);
            if (!m_world.GetModSystem().GetServerModId(pObject->formID, gameId.ModId, gameId.BaseId))
                continue;

            if (const auto pWorldSpace = pObject->GetWorldSpace())
            {
                if (!m_world.GetModSystem().GetServerModId(pWorldSpace->formID, objectData.WorldSpaceId.ModId, objectData.WorldSpaceId.BaseId))
                    continue;

                const auto* pTES = TES::Get();
                const auto* pCell = ModManager::Get()->GetCellFromCoordinates(pTES->currentGridX, pTES->currentGridY, pWorldSpace, 0);
                if (!m_world.GetModSystem().GetServerModId(pCell->formID, objectData.CellId.ModId, objectData.CellId.BaseId))
                    continue;

                objectData.CurrentCoords = GridCellCoords(pTES->currentGridX, pTES->currentGridY);
            }
            else if (const auto pParentCell = pObject->GetParentCell())
            {
                if (!m_world.GetModSystem().GetServerModId(pParentCell->formID, objectData.CellId.ModId, objectData.CellId.BaseId))
                    continue;
            }

            objectData.CurrentInventory.Buffer = pObject->SerializeInventory();

            message.Changes[gameId] = objectData;
        }

        m_transport.Send(message);

        m_objectsWithInventoryChanges.clear();

        spdlog::error("Sent request for inventory change");
    }
}

void InventoryService::RunCharacterInventoryUpdates() noexcept
{
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenSnapshots = 250ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenSnapshots)
        return;

    lastSendTimePoint = now;

    if (!m_charactersWithInventoryChanges.empty())
    {
        RequestCharacterInventoryChanges message;

        auto animatedLocalView = m_world.view<LocalComponent, LocalAnimationComponent, FormIdComponent>();
        for (auto entity : animatedLocalView)
        {
            auto& formIdComponent = animatedLocalView.get<FormIdComponent>(entity);
            auto& localComponent = animatedLocalView.get<LocalComponent>(entity);

            if (m_charactersWithInventoryChanges.find(formIdComponent.Id) == std::end(m_charactersWithInventoryChanges))
                continue;

            const auto* pForm = TESForm::GetById(formIdComponent.Id);
            const auto* pActor = RTTI_CAST(pForm, TESForm, Actor);
            if (!pActor)
                continue;

            message.Changes[localComponent.Id] = pActor->GetInventory();
        }

        m_transport.Send(message);

        m_charactersWithInventoryChanges.clear();
    }
}

void InventoryService::ApplyCachedObjectInventoryChanges() noexcept
{
    if (UI::Get()->IsOpen(BSFixedString("ContainerMenu")))
        return;

    if (m_cachedObjectInventoryChanges.empty())
        return;

    spdlog::warn("Applying object inventory changes");

    for (const auto& [id, inventory] : m_cachedObjectInventoryChanges)
    {
        const auto cObjectId = World::Get().GetModSystem().GetGameId(id);
        if (cObjectId == 0)
        {
            spdlog::error("Failed to retrieve object to sync inventory.");
            continue;
        }

        auto* pObject = RTTI_CAST(TESForm::GetById(cObjectId), TESForm, TESObjectREFR);
        if (!pObject)
        {
            spdlog::error("Failed to retrieve object to sync inventory.");
            continue;
        }

        pObject->RemoveAllItems();
        pObject->DeserializeInventory(inventory.Buffer);
    }

    m_cachedObjectInventoryChanges.clear();
}

void InventoryService::ApplyCachedCharacterInventoryChanges() noexcept
{
    if (UI::Get()->IsOpen(BSFixedString("ContainerMenu")))
        return;

    auto view = m_world.view<RemoteComponent, FormIdComponent>();
    for (const auto& [id, inventory] : m_cachedCharacterInventoryChanges)
    {
        const auto itor = std::find_if(std::begin(view), std::end(view), [id = id, view](entt::entity entity) {
            return view.get<RemoteComponent>(entity).Id == id;
        });

        if (itor != std::end(view))
        {
            auto& formIdComponent = view.get<FormIdComponent>(*itor);
            auto& remoteComponent = view.get<RemoteComponent>(*itor);

            auto* const pActor = RTTI_CAST(TESForm::GetById(formIdComponent.Id), TESForm, Actor);
            if (!pActor)
                return;

            remoteComponent.SpawnRequest.InventoryContent = inventory;
            pActor->SetInventory(inventory);
        }
    }
    m_cachedCharacterInventoryChanges.clear();
}
