#include "InstrumentTypeRegistry.h"

namespace polysynth
{

std::shared_ptr<const InstrumentType> InstrumentTypeRegistry::getInstrumentType(int typeId) const
{
    auto it = instrumentTypes.find(typeId);
    if (it == instrumentTypes.end())
    {
        return nullptr;
    }
    return it->second;
}

void InstrumentTypeRegistry::addInstrumentType(int typeId,
                                               std::unique_ptr<InstrumentType> instrumentType)
{
    instrumentTypes[typeId] = std::move(instrumentType);
}

void InstrumentTypeRegistry::removeInstrumentType(int typeId)
{
    instrumentTypes.erase(typeId);
}

int InstrumentTypeRegistry::getFallbackSynthTypeId() const
{
    for (const auto& [id, type] : instrumentTypes)
    {
        if (id > 0 && id < static_cast<int>(InstrumentTypeId::kDelay))
        {
            return id;
        }
    }
    return 1;
}

}
