#pragma once

namespace Raven::ph
{

// ============================================================================
// ElectricChargeComponent
// ============================================================================
// Entityが電荷を持つことをPhysicsへ公開する軽量Componentです。
// 電荷量の単位はSIのCoulomb [C] とし、正負の符号で電荷の極性を表します。
//
// 現段階では点電荷モデルのみを扱います。有限サイズの電荷分布や導体表面電荷は、
// ElectricField / ChargeDistributionを導入する段階で別の型へ分離します。
struct ElectricChargeComponent
{
    double ChargeCoulombs = 0.0;

    bool IsEnabled = true;
};

} // namespace Raven::ph
