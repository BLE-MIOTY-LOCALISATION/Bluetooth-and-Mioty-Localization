#pragma once

#include "Trx/SX1280.h"
#include "STM32TsUnb.h"

namespace TsUnbLib
{
    namespace STM32
    {
        typedef TsUnb::SimpleNode<TsUnb::FixedUplinkMac,
                                  TsUnb::Phy<12099962, 12099962, 12, 12, TsUnb::TsUnb_UPG1, 3, TsUnb::RadioBurst<2, 2>>,
                                  Trx::SX1280<STM32TsUnb<48>, 3, TsUnb::RadioBurst<2, 2>>>
            TsUnb_EU1_Lambda80_t;
    }
}
