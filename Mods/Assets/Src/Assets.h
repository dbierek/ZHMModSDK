#include "IPluginInterface.h"

class Assets : public IPluginInterface
{
public:
    Assets();

    void OnDrawMenu() override;
    void OnDrawUI(bool p_HasFocus) override;

private:
    void LoadRepositoryProps();
    std::string ConvertDynamicObjectValueTString(const ZDynamicObject& p_DynamicObject);

    bool m_MenuVisible;

    TResourcePtr<ZTemplateEntityFactory> m_RepositoryResource;
    std::multimap<std::string, ZRepositoryID> m_RepositoryProps;

    const std::vector<std::string> m_CharSetCharacterTypes = { "Actor", "Nude", "HeroA" };
};

DECLARE_ZHM_PLUGIN(Assets)