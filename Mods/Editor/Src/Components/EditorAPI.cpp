#include <Editor.h>

#include "Logging.h"

#include <Glacier/EntityFactory.h>
#include <Glacier/ZSpatialEntity.h>
#include <Glacier/ZPhysics.h>

#include <ResourceLib_HM3.h>

#include <queue>
#include <utility>
#include <numbers>

ZEntityRef Editor::FindEntity(EntitySelector p_Selector) {
    std::shared_lock s_Lock(m_CachedEntityTreeMutex);

    if (!m_CachedEntityTree) {
        return {};
    }

    // When TBLU hash is not set, we're selecting from spawned entities.
    if (!p_Selector.TbluHash.has_value()) {
        const auto s_Entity = m_SpawnedEntities.find(p_Selector.EntityId);

        if (s_Entity != m_SpawnedEntities.end()) {
            return s_Entity->second;
        }

        return {};
    }

    // Otherwise we're selecting from the entity tree.
    const ZRuntimeResourceID s_TBLU = p_Selector.TbluHash.value();

    // Create a queue and add the root to it.
    std::queue<std::shared_ptr<EntityTreeNode>> s_NodeQueue;
    s_NodeQueue.push(m_CachedEntityTree);

    // Keep track of the last node that matched the entity ID, but not the TBLU.
    std::shared_ptr<EntityTreeNode> s_IdMatchedNode = nullptr;

    // Keep iterating through the tree until we find the node we're looking for.
    while (!s_NodeQueue.empty()) {
        // Access the first node in the queue
        auto s_Node = s_NodeQueue.front();
        s_NodeQueue.pop();

        if (s_Node->EntityId == p_Selector.EntityId) {
            bool s_Matches = s_Node->TBLU == s_TBLU;

            // If the TBLU doesn't match, then check the owner entity.
            if (!s_Matches) {
                const auto s_OwningEntity = s_Node->Entity.GetOwningEntity();

                if (s_OwningEntity && s_OwningEntity.GetBlueprintFactory()) {
                    s_Matches = s_OwningEntity.GetBlueprintFactory()->m_ridResource == s_TBLU;
                }
            }

            // If it's not that either, try our own factory.
            if (!s_Matches) {
                const auto s_Factory = s_Node->Entity.GetBlueprintFactory();

                if (s_Factory) {
                    s_Matches = s_Factory->m_ridResource == s_TBLU;
                }
            }

            // Found the node we're looking for!
            if (s_Matches) {
                return s_Node->Entity;
            }

            // Otherwise, keep track of the last node that matched the entity ID.
            s_IdMatchedNode = s_Node;
        }

        // If not found, add children to the queue.
        for (auto& childPair : s_Node->Children) {
            s_NodeQueue.push(childPair.second);
        }
    }

    if (s_IdMatchedNode) {
        return s_IdMatchedNode->Entity;
    }

    return {};
}

std::string Editor::getCollisionHash(auto s_SelectedEntity) {
	const auto s_EntityType = s_SelectedEntity->GetType();
	std::string s_AlocHash = "";
	if (s_EntityType && s_EntityType->m_pProperties01) {
		for (uint32_t i = 0; i < s_EntityType->m_pProperties01->size(); ++i) {
			ZEntityProperty* s_Property = &s_EntityType->m_pProperties01->operator[](i);
			const auto* s_PropertyInfo = s_Property->m_pType->getPropertyInfo();

			if (!s_PropertyInfo || !s_PropertyInfo->m_pType)
				continue;

			const auto s_PropertyAddress = reinterpret_cast<uintptr_t>(s_SelectedEntity.m_pEntity) + s_Property->m_nOffset;
			const uint16_t s_TypeSize = s_PropertyInfo->m_pType->typeInfo()->m_nTypeSize;
			const uint16_t s_TypeAlignment = s_PropertyInfo->m_pType->typeInfo()->m_nTypeAlignment;

			const std::string s_TypeName = s_PropertyInfo->m_pType->typeInfo()->m_pTypeName;
			const std::string s_InputId = std::format("##Property{}", i);

			const char* s_COLLISION_RESOURCE_ID_PROPERTY_NAME = "m_CollisionResourceID";
			
			if (s_PropertyInfo->m_pType->typeInfo()->isResource() || s_PropertyInfo->m_nPropertyID != s_Property->m_nPropertyId) {
				// Some properties don't have a name for some reason. Try to find using RL.
				const auto s_PropertyNameView = HM3_GetPropertyName(s_Property->m_nPropertyId);

				if (s_PropertyNameView.Size > 0) {
					if (std::string(s_PropertyNameView.Data, s_PropertyNameView
						.Size) != s_COLLISION_RESOURCE_ID_PROPERTY_NAME) {
						continue;
					} else {
                        // Get the value of the property.
                        auto* s_Data = (*Globals::MemoryManager)->m_pNormalAllocator->AllocateAligned(s_TypeSize, s_TypeAlignment);

                        if (s_PropertyInfo->m_nFlags & EPropertyInfoFlags::E_HAS_GETTER_SETTER) {
                            s_PropertyInfo->get(
                                reinterpret_cast<void*>(s_PropertyAddress),
                                s_Data,
                                s_PropertyInfo->m_nOffset);
                        }
                        else {
                            s_PropertyInfo->m_pType->typeInfo()->m_pTypeFunctions->copyConstruct(
                                s_Data,
                                reinterpret_cast<void*>(s_PropertyAddress));
                        }
						//Logger::Info("Property Name: {}", std::string(s_PropertyNameView.Data, s_PropertyNameView.Size).c_str());
						auto* s_Resource = static_cast<ZResourcePtr*>(s_Data);
						std::string s_ResourceName = "null";

						if (s_Resource && s_Resource->m_nResourceIndex >= 0) {
							s_ResourceName = fmt::format("{:08X}{:08X}", s_Resource->GetResourceInfo().rid.m_IDHigh, s_Resource->GetResourceInfo().rid.m_IDLow);
						}
                        (*Globals::MemoryManager)->m_pNormalAllocator->Free(s_Data);

						//Logger::Info("Found ALOC Resource: {}", s_ResourceName.c_str());
						if (s_ResourceName.c_str() != "" && s_ResourceName.c_str() != NULL && s_ResourceName.c_str() != "null") { 
							return s_ResourceName.c_str();
						}
					}
					
				}
			}
		}
	}

	return "";
}

auto* Editor::GetProperty(ZEntityRef p_Entity, ZEntityProperty* p_Property) {
	const auto* s_PropertyInfo = p_Property->m_pType->getPropertyInfo();
	const auto s_PropertyAddress = reinterpret_cast<uintptr_t>(p_Entity.m_pEntity) + p_Property->m_nOffset;
	const uint16_t s_TypeSize = s_PropertyInfo->m_pType->typeInfo()->m_nTypeSize;
	const uint16_t s_TypeAlignment = s_PropertyInfo->m_pType->typeInfo()->m_nTypeAlignment;

	// Get the value of the property.
	auto* s_Data = (*Globals::MemoryManager)->m_pNormalAllocator->AllocateAligned(s_TypeSize, s_TypeAlignment);

	if (s_PropertyInfo->m_nFlags & EPropertyInfoFlags::E_HAS_GETTER_SETTER)
		s_PropertyInfo->get(reinterpret_cast<void*>(s_PropertyAddress), s_Data, s_PropertyInfo->m_nOffset);
	else
		s_PropertyInfo->m_pType->typeInfo()->m_pTypeFunctions->copyConstruct(s_Data, reinterpret_cast<void*>(s_PropertyAddress));
	return s_Data;
}

Quat Editor::GetQuatFromProperty(ZEntityRef p_Entity) {

	const std::string s_TransformPropertyName = "m_mTransform";
	const auto s_EntityType = p_Entity->GetType();

	for (uint32_t i = 0; i < s_EntityType->m_pProperties01->size(); ++i) {
		ZEntityProperty* s_Property = &s_EntityType->m_pProperties01->operator[](i);
		const auto* s_PropertyInfo = s_Property->m_pType->getPropertyInfo();

		if (s_PropertyInfo->m_pType->typeInfo()->isResource() || s_PropertyInfo->m_nPropertyID != s_Property->m_nPropertyId) {
			// Some properties don't have a name for some reason. Try to find using RL.
			const auto s_PropertyName = HM3_GetPropertyName(s_Property->m_nPropertyId);

			if (s_PropertyName.Size > 0) {
				std::string_view s_PropertyNameView = std::string_view(s_PropertyName.Data, s_PropertyName.Size);
				if (s_PropertyNameView == s_TransformPropertyName) {
					SMatrix43* s_Data43 = reinterpret_cast<SMatrix43*>(GetProperty(p_Entity, s_Property));
					SMatrix s_Data = SMatrix(*s_Data43);
					const auto s_Decomposed = s_Data.Decompose();
					const auto s_Quat = s_Decomposed.Quaternion;
					return s_Quat;
				}
			}
		} else if (s_PropertyInfo->m_pName && s_PropertyInfo->m_pName == s_TransformPropertyName) {
			SMatrix43* s_Data43 = reinterpret_cast<SMatrix43*>(GetProperty(p_Entity, s_Property));
			SMatrix s_Data = SMatrix(*s_Data43);
			const auto s_Decomposed = s_Data.Decompose();
			const auto s_Quat = s_Decomposed.Quaternion;
			return s_Quat;
		}
	}
	return Quat();
}


Quat Editor::GetParentQuat(ZEntityRef p_Entity) {
	ZSpatialEntity* s_Entity = p_Entity.QueryInterface<ZSpatialEntity>();
	TEntityRef<ZSpatialEntity> s_EidParent;
	std::vector<Quat> s_ParentQuats;
	while (s_Entity->m_eidParent != NULL) {
		s_EidParent = s_Entity->m_eidParent;
		std::string s_Id = std::format("{:016x}", s_EidParent.m_ref->GetType()->m_nEntityId);
		//Logger::Info("Parent id: '{}'", s_Id);

		s_Entity = s_EidParent.m_pInterfaceRef;

		s_ParentQuats.push_back(GetQuatFromProperty(s_EidParent.m_ref));
		
	}

	if (s_ParentQuats.empty()) {
		return Quat();
	}
	std::reverse(s_ParentQuats.begin(), s_ParentQuats.end());
	std::vector<Quat>::iterator s_QuatIter = s_ParentQuats.begin();
	Quat s_Quat = *s_QuatIter;
	while (s_QuatIter != s_ParentQuats.end()) {
		if (s_QuatIter != s_ParentQuats.begin()) {
			s_Quat = s_Quat * *s_QuatIter;
		}
		s_QuatIter++;
	}
	return s_Quat;
}

void Editor::FindAlocs(std::function<void(std::vector<std::tuple<std::vector<std::string>, Quat, ZEntityRef>>, bool s_Done)> s_SendEntitiesCallback) {
	std::shared_lock s_Lock(m_CachedEntityTreeMutex);

	if (!m_CachedEntityTree) {
		return;
	}
	std::vector<std::tuple<std::vector<std::string>, Quat, ZEntityRef>> entities;

	// Create a queue and add the root to it.
	std::queue<std::pair<std::shared_ptr<EntityTreeNode>, std::shared_ptr<EntityTreeNode>>> s_NodeQueue;
	s_NodeQueue.push(std::pair<std::shared_ptr<EntityTreeNode>, std::shared_ptr<EntityTreeNode>>{std::shared_ptr<EntityTreeNode>(), m_CachedEntityTree});
	const char* s_GEOMENTITY_TYPE = "ZGeomEntity";
	const char* s_PRIMITIVEPROXY_TYPE = "ZPrimitiveProxyEntity";
	const char* s_PURE_WATER_TYPE = "ZPureWaterAspect";
	std::vector<std::string> s_selectorPrimHashes;
	// Keep iterating through the tree until we find all the prims.
	while (!s_NodeQueue.empty()) {
        if (entities.size() >= 10) {
            s_SendEntitiesCallback(entities, false);
            entities.clear();
        }
		// Access the first node in the queue
		auto s_Parent = s_NodeQueue.front().first;
		auto s_Node = s_NodeQueue.front().second;
		s_NodeQueue.pop();
        std::string s_Id = std::format("{:016x}", s_Node->Entity->GetType()->m_nEntityId);
		const auto& s_Interfaces = *s_Node->Entity.GetEntity()->GetType()->m_pInterfaces;
        const auto typeInfo = s_Interfaces[0].m_pTypeId->typeInfo();
        if (typeInfo == NULL) {
            continue;
        }
		char* s_EntityType = typeInfo->m_pTypeName;
        if (strcmp(s_EntityType, s_GEOMENTITY_TYPE) == 0) {
            if (const ZGeomEntity* s_GeomEntity = s_Node->Entity.QueryInterface<ZGeomEntity>()) {
                std::string s_HashString = std::format("<{:08X}{:08X}>", s_Node->TBLU.m_IDHigh, s_Node->TBLU.m_IDLow);

                std::vector<std::string> s_AlocHashes;
                ZResourceIndex s_ResourceIndex(s_GeomEntity->m_ResourceID.m_nResourceIndex);
                TArray<ZResourceIndex> s_Indices;
                TArray<unsigned char> s_Flags;
                if (s_ResourceIndex.val != -1) {
                    Functions::ZResourceContainer_GetResourceReferences->Call(*Globals::ResourceContainer, s_ResourceIndex, s_Indices, s_Flags);
                    for (ZResourceIndex s_CurrentResourceIndex : s_Indices) {
                        const auto s_ReferenceResourceInfo = (*Globals::ResourceContainer)->m_resources[s_GeomEntity->m_ResourceID.m_nResourceIndex];
                        if (s_ReferenceResourceInfo.resourceType == 'ALOC') {
                            const auto s_AlocHash = s_ReferenceResourceInfo.rid.GetID();
                            std::string s_AlocHashString{ std::format("{:016X}", s_AlocHash) };
                            s_AlocHashes.push_back(s_AlocHashString);
                            Logger::Info("Found ALOC. ID: {} TBLU: {} ALOC: {}", s_Id, s_HashString, s_AlocHashString);
                        }
                    }
                    const auto s_PrimResourceInfo = (*Globals::ResourceContainer)->m_resources[s_GeomEntity->m_ResourceID.m_nResourceIndex];
                    const auto s_PrimHash = s_PrimResourceInfo.rid.GetID();
                    std::string s_PrimHashString{ std::format("{:016X}", s_PrimHash) };
                    std::string s_collision_ioi_string = getCollisionHash(s_Node->Entity);
                    if (!s_collision_ioi_string.empty() && s_collision_ioi_string != "null") {
                        bool s_Skip = false;
                        for (auto s_Interface : s_Interfaces) {
                            if (s_Interface.m_pTypeId->typeInfo() != NULL) {
                                char* s_EntityType = s_Interface.m_pTypeId->typeInfo()->m_pTypeName;
                                if (strcmp(s_EntityType, s_PURE_WATER_TYPE) == 0) {
                                    s_Skip = true;
                                    break;
                                }
                            }
                        }
                        if (!s_Skip) {
                            Logger::Info("Found ALOC. ID: {} TBLU: {} PRIM: {} ALOC: {}", s_Id, s_HashString, s_PrimHashString, s_collision_ioi_string);
                            s_AlocHashes.push_back(s_collision_ioi_string);
                            Quat s_EntityQuat = GetQuatFromProperty(s_Node->Entity);
                            Quat s_ParentQuat = GetParentQuat(s_Node->Entity);

                            Quat s_CombinedQuat;
                            s_CombinedQuat = s_ParentQuat * s_EntityQuat;
                            std::tuple<std::vector<std::string>, Quat, ZEntityRef> s_Entity =
                                std::make_tuple(
                                    s_AlocHashes,
                                    s_CombinedQuat,
                                    s_Node->Entity
                                );
                            entities.push_back(s_Entity);
                        }
                    }
                }
            }
        }
        else if (strcmp(s_EntityType, s_PRIMITIVEPROXY_TYPE) == 0) {
            std::string s_HashString = std::format("<{:08X}{:08X}>", s_Node->TBLU.m_IDHigh, s_Node->TBLU.m_IDLow);

            std::vector<std::string> s_AlocHashes;
            std::string s_collision_ioi_string = getCollisionHash(s_Node->Entity);
            if (!s_collision_ioi_string.empty() && s_collision_ioi_string != "null") {
                bool s_Skip = false;
                for (auto s_Interface : s_Interfaces) {
                    if (s_Interface.m_pTypeId->typeInfo() != NULL) {
                        char* s_EntityType = s_Interface.m_pTypeId->typeInfo()->m_pTypeName;
                        if (strcmp(s_EntityType, s_PURE_WATER_TYPE) == 0) {
                            s_Skip = true;
                            break;
                        }
                    }
                }
                if (!s_Skip) {
                    Logger::Info("Found ALOC. ID: {} TBLU: {} ALOC: {}", s_Id, s_HashString, s_collision_ioi_string);
                    s_AlocHashes.push_back(s_collision_ioi_string);
                    Quat s_EntityQuat = GetQuatFromProperty(s_Node->Entity);
                    Quat s_ParentQuat = GetParentQuat(s_Node->Entity);

                    Quat s_CombinedQuat;
                    s_CombinedQuat = s_ParentQuat * s_EntityQuat;
                    std::tuple<std::vector<std::string>, Quat, ZEntityRef> s_Entity =
                        std::make_tuple(
                            s_AlocHashes,
                            s_CombinedQuat,
                            s_Node->Entity
                        );
                    entities.push_back(s_Entity);
                }
            }
        }

		// Add children to the queue.
		for (auto& s_ChildPair: s_Node->Children) {
            std::string s_ChildId = std::format("{:016x}", s_ChildPair.second->Entity->GetType()->m_nEntityId);

            s_NodeQueue.push(std::pair<std::shared_ptr<EntityTreeNode>, std::shared_ptr<EntityTreeNode>>{s_Node, s_ChildPair.second});
		}
	}
    s_SendEntitiesCallback(entities, true);
    entities.clear();

	return;
}

std::vector<std::tuple<std::vector<std::string>, Quat, ZEntityRef>> Editor::FindPfSeedPointEntities() {
	std::shared_lock s_Lock(m_CachedEntityTreeMutex);

	if (!m_CachedEntityTree) {
		return {};
	}
	std::vector<std::tuple<std::vector<std::string>, Quat, ZEntityRef>> entities;
	const char* s_PFSEEDPOINT_TYPE = "ZPFSeedPoint";

	Logger::Info("Getting PfSeedPoint Entities:");
	// Create a queue and add the root to it.
	std::queue<std::shared_ptr<EntityTreeNode>> s_NodeQueue;
	s_NodeQueue.push(m_CachedEntityTree);

	// Keep iterating through the tree until we find the nodes we're looking for.
	while (!s_NodeQueue.empty()) {
		// Access the first node in the queue
		auto s_Node = s_NodeQueue.front();
		s_NodeQueue.pop();
		const auto& s_Interfaces = *s_Node->Entity.GetEntity()->GetType()->m_pInterfaces;
		char* s_EntityType = s_Interfaces[0].m_pTypeId->typeInfo()->m_pTypeName;

		if (strcmp(s_EntityType, s_PFSEEDPOINT_TYPE) == 0) {
			Quat s_EntityQuat = GetQuatFromProperty(s_Node->Entity);
			Quat s_ParentQuat = GetParentQuat(s_Node->Entity);

			Quat s_CombinedQuat;
			s_CombinedQuat = s_ParentQuat * s_EntityQuat;
			std::tuple<std::vector<std::string>, Quat, ZEntityRef> s_Entity =
			    std::make_tuple(
                    std::vector<std::string>{ "00280B8C4462FAC8" },
			        s_CombinedQuat,
			        s_Node->Entity);

			entities.push_back(s_Entity);
		}

		// Add children to the queue.
		for (auto& s_ChildPair: s_Node->Children) {
			s_NodeQueue.push(s_ChildPair.second);
		}
	}

	return entities;
}

std::vector<std::tuple<std::vector<std::string>, Quat, ZEntityRef>> Editor::FindPfBoxEntities() {
	std::shared_lock s_Lock(m_CachedEntityTreeMutex);

	if (!m_CachedEntityTree) {
		return {};
	}
	std::vector<std::tuple<std::vector<std::string>, Quat, ZEntityRef>> entities;
	const char* s_PFBOXENTITY_TYPE = "ZPFBoxEntity";

	Logger::Info("Getting PfBoxEntities:");
	// Create a queue and add the root to it.
	std::queue<std::shared_ptr<EntityTreeNode>> s_NodeQueue;
	s_NodeQueue.push(m_CachedEntityTree);

	// Keep iterating through the tree until we find the nodes we're looking for.
	while (!s_NodeQueue.empty()) {
		// Access the first node in the queue
		auto s_Node = s_NodeQueue.front();
		s_NodeQueue.pop();
		const auto& s_Interfaces = *s_Node->Entity.GetEntity()->GetType()->m_pInterfaces;
		char* s_EntityType = s_Interfaces[0].m_pTypeId->typeInfo()->m_pTypeName;

		if (strcmp(s_EntityType, s_PFBOXENTITY_TYPE) == 0) {
			Quat s_EntityQuat = GetQuatFromProperty(s_Node->Entity);
			Quat s_ParentQuat = GetParentQuat(s_Node->Entity);

			Quat s_CombinedQuat;
			s_CombinedQuat = s_ParentQuat * s_EntityQuat;
			std::tuple<std::vector<std::string>, Quat, ZEntityRef> s_Entity =
			    std::make_tuple(
                    std::vector<std::string>{ "00724CDE424AFE76" },
			        s_CombinedQuat,
			        s_Node->Entity);

			entities.push_back(s_Entity);
		}

		// Add children to the queue.
		for (auto& s_ChildPair: s_Node->Children) {
			s_NodeQueue.push(s_ChildPair.second);
		}
	}

	return entities;
}

void Editor::SelectEntity(EntitySelector p_Selector, std::optional<std::string> p_ClientId) {
    auto s_Entity = FindEntity(p_Selector);

    if (s_Entity) {
        OnSelectEntity(s_Entity, std::move(p_ClientId));
    }
    else {
        throw std::runtime_error("Could not find entity for the given selector.");
    }
}

void Editor::SetEntityTransform(
    EntitySelector p_Selector, SMatrix p_Transform, bool p_Relative, std::optional<std::string> p_ClientId
) {
    if (const auto s_Entity = FindEntity(p_Selector)) {
        OnEntityTransformChange(s_Entity, p_Transform, p_Relative, std::move(p_ClientId));
    }
    else {
        throw std::runtime_error("Could not find entity for the given selector.");
    }
}

void Editor::SpawnEntity(
    ZRuntimeResourceID p_Template, uint64_t p_EntityId, std::string p_Name, std::optional<std::string> p_ClientId
) {}

void Editor::DestroyEntity(EntitySelector p_Selector, std::optional<std::string> p_ClientId) {}

void Editor::SetEntityName(EntitySelector p_Selector, std::string p_Name, std::optional<std::string> p_ClientId) {
    if (const auto s_Entity = FindEntity(p_Selector)) {
        OnEntityNameChange(s_Entity, p_Name, std::move(p_ClientId));
    }
    else {
        throw std::runtime_error("Could not find entity for the given selector.");
    }
}

void Editor::SetEntityProperty(
    EntitySelector p_Selector, uint32_t p_PropertyId, std::string_view p_JsonValue,
    std::optional<std::string> p_ClientId
) {
    if (const auto s_Entity = FindEntity(p_Selector)) {
        auto s_EntityType = s_Entity->GetType();
        auto s_Property = s_EntityType->FindProperty(p_PropertyId);

        if (!s_Property) {
            throw std::runtime_error("Could not find property for the given ID.");
        }

        if (!s_Property->m_pType || !s_Property->m_pType->getPropertyInfo() || !s_Property->m_pType->getPropertyInfo()->
            m_pType) {
            throw std::runtime_error(
                "Unable to set this property because its type information is missing from the game."
            );
        }

        const auto s_PropertyInfo = s_Property->m_pType->getPropertyInfo();

        if (s_PropertyInfo->m_pType->typeInfo()->isEntity()) {
            if (p_JsonValue == "null") {
                auto s_EntityRefObj = ZObjectRef::From<TEntityRef<ZEntityImpl>>({});
                s_EntityRefObj.UNSAFE_SetType(s_PropertyInfo->m_pType);

                OnSetPropertyValue(
                    s_Entity, p_PropertyId, s_EntityRefObj, std::move(p_ClientId)
                );
            }
            else {
                // Parse EntitySelector
                simdjson::ondemand::parser s_Parser;
                const auto s_EntitySelectorJson = simdjson::padded_string(p_JsonValue);
                simdjson::ondemand::document s_EntitySelectorMsg = s_Parser.iterate(s_EntitySelectorJson);

                const auto s_EntitySelector = EditorServer::ReadEntitySelector(s_EntitySelectorMsg);

                if (const auto s_TargetEntity = FindEntity(s_EntitySelector)) {
                    auto s_EntityRefObj = ZObjectRef::From<TEntityRef<ZEntityImpl>>(
                        TEntityRef<ZEntityImpl>(s_TargetEntity)
                    );
                    s_EntityRefObj.UNSAFE_SetType(s_PropertyInfo->m_pType);

                    OnSetPropertyValue(
                        s_Entity, p_PropertyId, s_EntityRefObj, std::move(p_ClientId)
                    );
                }
                else {
                    throw std::runtime_error("Could not find entity for the given selector.");
                }
            }
        }
        else {
            const uint16_t s_TypeSize = s_PropertyInfo->m_pType->typeInfo()->m_nTypeSize;
            const uint16_t s_TypeAlignment = s_PropertyInfo->m_pType->typeInfo()->m_nTypeAlignment;
            const std::string s_TypeName = s_PropertyInfo->m_pType->typeInfo()->m_pTypeName;

            void* s_Data = (*Globals::MemoryManager)->m_pNormalAllocator->AllocateAligned(
                s_TypeSize,
                s_TypeAlignment
            );

            const bool s_Success = HM3_JsonToGameStruct(
                s_TypeName.c_str(),
                p_JsonValue.data(),
                p_JsonValue.size(),
                s_Data,
                s_TypeSize
            );

            if (!s_Success) {
                (*Globals::MemoryManager)->m_pNormalAllocator->Free(s_Data);
                throw std::runtime_error("Unable to convert JSON to game struct.");
            }

            ZObjectRef s_DataObj;
            s_DataObj.UNSAFE_Assign(s_PropertyInfo->m_pType, s_Data);

            OnSetPropertyValue(
                s_Entity, p_PropertyId, s_DataObj, std::move(p_ClientId)
            );
        }
    }
    else {
        throw std::runtime_error("Could not find entity for the given selector.");
    }
}

void Editor::SignalEntityPin(EntitySelector p_Selector, uint32_t p_PinId, bool p_Output) {
    if (const auto s_Entity = FindEntity(p_Selector)) {
        OnSignalEntityPin(s_Entity, p_PinId, p_Output);
    }
    else {
        throw std::runtime_error("Could not find entity for the given selector.");
    }
}

void Editor::RebuildEntityTree() {
	UpdateEntities();
}

void Editor::LoadNavpAreas(simdjson::ondemand::array p_NavpAreas, int p_ChunkIndex) {
	Logger::Info("Loading Navp areas");	

	if (p_ChunkIndex == 0) {
		m_NavpAreas.clear();
	}
	for (simdjson::ondemand::array s_NavpArea: p_NavpAreas) {
		std::vector<SVector3> s_Area;
		for (simdjson::ondemand::array s_NavpPoint: s_NavpArea) {
			std::vector<double> s_Point;
			for (double coord: s_NavpPoint) {
				s_Point.push_back(coord);
			}
			SVector3 point{(float) s_Point[0], (float) s_Point[1], (float) s_Point[2]};

			s_Area.push_back(point);
		}
		m_NavpAreas.push_back(s_Area);
	}
}