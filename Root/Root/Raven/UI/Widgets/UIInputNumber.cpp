#include "Raven/UI/Widgets/UIInputNumber.h"

namespace Raven
{
bool UIInputNumber::SetValueIfChanged(double value)
{
    if (std::isfinite(value) == false)
    {
        return false;
    }
    const double next = std::clamp(value, m_Min, m_Max);
    if (m_Value == next && m_CommittedValue == next)
    {
        return false;
    }
    SetValue(next);
    return true;
}
bool UIInputNumber::SetRangeIfChanged(double minimum, double maximum)
{
    if (std::isfinite(minimum) == false || std::isfinite(maximum) == false ||
        minimum > maximum || (m_Min == minimum && m_Max == maximum))
    {
        return false;
    }
    SetRange(minimum, maximum);
    return true;
}
} // namespace Raven
