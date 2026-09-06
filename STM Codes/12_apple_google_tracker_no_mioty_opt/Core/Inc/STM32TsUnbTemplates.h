#pragma once

#include "TsUnb/RadioBurst.h"
#include "TsUnb/Phy.h"
#include "TsUnb/FixedMac.h"
#include "TsUnb/SimpleNode.h"
#include "Trx/SX1280.h"
#include "STM32TsUnb.h"

// -----------------------------------------------------------------------
// TsUnb_EU1_Lambda80_t - Mioty (TS-UNB) node type for STM32 + SX1280
//
// Template parameters:
//   FixedUplinkMac       - simple fixed-address uplink MAC
//   Phy<12099962, ...>   - TS-UNB physical layer (2.4 GHz band parameters)
//   SX1280<STM32TsUnb>   - SX1280 radio driver using STM32 HAL SPI wrapper
//   SYMBOL_RATE_MULT=48  - Lambda80 symbol rate multiplier (480 bps raw)
//
// This type is used as: TsUnb_EU1_Lambda80_t TsUnb_Node;
// -----------------------------------------------------------------------
namespace TsUnbLib
{
    namespace STM32
    {
        typedef TsUnb::SimpleNode<TsUnb::FixedUplinkMac,
                                  TsUnb::Phy<12100214+380, 12100214+380, 12, 12, TsUnb::TsUnb_UPG1, 3, TsUnb::RadioBurst<2, 2>>,
                                  Trx::SX1280<STM32TsUnb<48>, 3, TsUnb::RadioBurst<2, 2>>>
            TsUnb_EU1_Lambda80_t;
    }
}
