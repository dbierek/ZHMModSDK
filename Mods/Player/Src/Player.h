#include "IPluginInterface.h"

#include <Glacier/ZOutfit.h>

class Player : public IPluginInterface
{
public:
    Player();

    void OnDrawMenu() override;
    void OnDrawUI(bool p_HasFocus) override;

private:
    static void EquipOutfit(
        const TEntityRef<ZGlobalOutfitKit>& p_GlobalOutfitKit, uint8_t n_CurrentCharSetIndex,
        const std::string& s_CurrentCharSetCharacterType, uint8_t n_CurrentOutfitVariationIndex, ZHitman5* p_LocalHitman
    );

    static void EnableInfiniteAmmo();

    bool m_MenuVisible;

    ZHM5CrippleBox* m_Hm5CrippleBox = nullptr;

    const std::vector<std::string> m_CharSetCharacterTypes = { "Actor", "Nude", "HeroA" };
    TEntityRef<ZGlobalOutfitKit>* m_GlobalOutfitKit = nullptr;
};

DECLARE_ZHM_PLUGIN(Player)