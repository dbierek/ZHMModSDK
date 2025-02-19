#include "IPluginInterface.h"

#include <Glacier/EntityFactory.h>

class Items : public IPluginInterface
{
public:
    Items();

    void OnDrawMenu() override;
    void OnDrawUI(bool p_HasFocus) override;

private:
    std::string ConvertDynamicObjectValueTString(const ZDynamicObject& p_DynamicObject);

    bool m_MenuVisible;

    TResourcePtr<ZTemplateEntityFactory> m_RepositoryResource;
};

DECLARE_ZHM_PLUGIN(Items)