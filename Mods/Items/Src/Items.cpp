#include "Items.h"

#include <Glacier/ZAction.h>
#include <Glacier/ZItem.h>

Items::Items() : m_MenuVisible(false)
{
}

void Items::OnDrawMenu()
{
    if (ImGui::Button("Items"))
    {
        m_MenuVisible = !m_MenuVisible;
    }
}

void Items::OnDrawUI(bool p_HasFocus)
{
    if (!p_HasFocus || !m_MenuVisible) {
        return;
    }

    ImGui::PushFont(SDK()->GetImGuiBlackFont());
    const auto s_Showing = ImGui::Begin("Items", &m_MenuVisible);
    ImGui::PushFont(SDK()->GetImGuiRegularFont());

    if (s_Showing && p_HasFocus) {
        THashMap<ZRepositoryID, ZDynamicObject, TDefaultHashMapPolicy<ZRepositoryID>>* repositoryData = nullptr;

        if (m_RepositoryResource.m_nResourceIndex == -1) {
            const auto s_ID = ResId<"[assembly:/repository/pro.repo].pc_repo">;

            Globals::ResourceManager->GetResourcePtr(m_RepositoryResource, s_ID, 0);
        }

        if (m_RepositoryResource.GetResourceInfo().status == RESOURCE_STATUS_VALID) {
            repositoryData = static_cast<THashMap<ZRepositoryID, ZDynamicObject, TDefaultHashMapPolicy<ZRepositoryID>>*>
                (m_RepositoryResource.GetResourceData());
        }
        else {
            ImGui::PopFont();
            ImGui::End();
            ImGui::PopFont();

            return;
        }

        const ZHM5ActionManager* s_Hm5ActionManager = Globals::HM5ActionManager;
        std::vector<ZHM5Action*> s_Actions;

        if (s_Hm5ActionManager->m_Actions.size() == 0) {
            ImGui::PopFont();
            ImGui::End();
            ImGui::PopFont();

            return;
        }

        for (unsigned int i = 0; i < s_Hm5ActionManager->m_Actions.size(); ++i) {
            ZHM5Action* s_Action = s_Hm5ActionManager->m_Actions[i];

            if (s_Action && s_Action->m_eActionType == EActionType::AT_PICKUP) {
                s_Actions.push_back(s_Action);
            }
        }

        static size_t s_Selected = 0;

        ImGui::BeginChild("left pane", ImVec2(300, 0), true, ImGuiWindowFlags_HorizontalScrollbar);

        for (size_t i = 0; i < s_Actions.size(); i++) {
            const ZHM5Action* s_Action = s_Actions[i];
            const ZHM5Item* s_Item = s_Action->m_Object.QueryInterface<ZHM5Item>();
            std::string s_Title = std::format("{} {}", s_Item->m_pItemConfigDescriptor->m_sTitle.c_str(), i + 1);

            if (ImGui::Selectable(s_Title.c_str(), s_Selected == i)) {
                s_Selected = i;
            }
        }

        ImGui::EndChild();
        ImGui::SameLine();

        ImGui::BeginGroup();
        ImGui::BeginChild("item view", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));

        const ZHM5Action* s_Action = s_Actions[s_Selected];
        const ZHM5Item* s_Item = s_Action->m_Object.QueryInterface<ZHM5Item>();
        const ZDynamicObject* s_DynamicObject = &repositoryData->find(s_Item->m_pItemConfigDescriptor->m_RepositoryId)->
            second;
        const auto s_Entries = s_DynamicObject->As<TArray<SDynamicObjectKeyValuePair>>();
        std::string s_Image;

        for (size_t i = 0; i < s_Entries->size(); ++i) {
            std::string s_Key = s_Entries->operator[](i).sKey.c_str();

            if (s_Key == "Image") {
                s_Image = ConvertDynamicObjectValueTString(s_Entries->at(i).value);

                break;
            }
        }

        /*if (m_TextureResourceData.size() == 0)
        {
            const unsigned long long s_DdsTextureHash = GetDDSTextureHash(s_Image);

            LoadResourceData(s_DdsTextureHash, m_TextureResourceData);

            SDK()->LoadTextureFromMemory(m_TextureResourceData, &m_TextureSrvGpuHandle, m_Width, m_Height);
        }

        ImGui::Image(reinterpret_cast<ImTextureID>(m_TextureSrvGpuHandle.ptr), ImVec2(static_cast<float>(m_Width / 2), static_cast<float>(m_Height / 2)));*/

        for (unsigned int i = 0; i < s_Entries->size(); ++i) {
            std::string s_Key = std::format("{}:", s_Entries->operator[](i).sKey.c_str());
            const IType* s_Type = s_Entries->operator[](i).value.m_pTypeID->typeInfo();

            if (strcmp(s_Type->m_pTypeName, "TArray<ZDynamicObject>") == 0) {
                s_Key += " [";

                ImGui::Text(s_Key.c_str());

                const TArray<ZDynamicObject>* s_Array = s_Entries->operator[](i).value.As<TArray<ZDynamicObject>>();

                for (unsigned int j = 0; j < s_Array->size(); ++j) {
                    std::string s_Value = ConvertDynamicObjectValueTString(s_Array->at(j));

                    if (!s_Value.empty()) {
                        ImGui::Text(std::format("\t{}", s_Value).c_str());
                    }
                }

                ImGui::Text("]");
            }
            else {
                ImGui::Text(s_Key.c_str());

                std::string s_Value = ConvertDynamicObjectValueTString(s_Entries->at(i).value);

                ImGui::SameLine();
                ImGui::Text(s_Value.c_str());
            }
        }

        if (ImGui::Button("Teleport Item To Player")) {
            if (auto s_LocalHitman = SDK()->GetLocalPlayer()) {
                ZSpatialEntity* s_HitmanSpatial = s_LocalHitman.m_ref.QueryInterface<ZSpatialEntity>();
                s_Item->m_rGeomentity.m_pInterfaceRef->SetWorldMatrix(s_HitmanSpatial->GetWorldMatrix());
            }
        }

        ImGui::EndChild();
        ImGui::EndGroup();
    }

    ImGui::PopFont();
    ImGui::End();
    ImGui::PopFont();
}

std::string Items::ConvertDynamicObjectValueTString(const ZDynamicObject& p_DynamicObject) {
    std::string s_Result;
    const IType* s_Type = p_DynamicObject.m_pTypeID->typeInfo();

    if (strcmp(s_Type->m_pTypeName, "ZString") == 0) {
        const auto s_Value = p_DynamicObject.As<ZString>();
        s_Result = s_Value->c_str();
    }
    else if (strcmp(s_Type->m_pTypeName, "bool") == 0) {
        if (*p_DynamicObject.As<bool>()) {
            s_Result = "true";
        }
        else {
            s_Result = "false";
        }
    }
    else if (strcmp(s_Type->m_pTypeName, "float64") == 0) {
        double value = *p_DynamicObject.As<double>();

        s_Result = std::to_string(value).c_str();
    }
    else {
        s_Result = s_Type->m_pTypeName;
    }

    return s_Result;
}

DEFINE_ZHM_PLUGIN(Items);
