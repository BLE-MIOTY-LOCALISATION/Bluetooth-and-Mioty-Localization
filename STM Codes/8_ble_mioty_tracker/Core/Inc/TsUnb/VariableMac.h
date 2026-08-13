/* -----------------------------------------------------------------------------

Software License for the Fraunhofer TS-UNB-Lib

(c) Copyright  2019 - 2021 Fraunhofer-Gesellschaft zur Foerderung der angewandten
Forschung e.V. All rights reserved.

1. INTRODUCTION

The Fraunhofer Telegram Splitting - Ultra Narrowband Library ("TS-UNB-Lib") is software 
that implements the ETSI TS 103 357 TS-UNB standard ("MIOTY") for wireless data 
transmission in the field of IoT. Patent licenses for necessary patent claims for 
the ETSI TS 103 357 TS-UNB standard (including those of Fraunhofer) may be obtained
through Sisvel International S.A. 
(https://www.sisvel.com/licensing-programs/wireless-communications/mioty/license-terms) 
or through the respective patent owners individually. 
Commercially-licensed MIOTY software is also available from Fraunhofer. Users are
encouraged to check the Fraunhofer website for additional applications
information and documentation.

2. COPYRIGHT LICENSE

Redistribution and use in source and binary forms, with or without modification,
are permitted without payment of copyright license fees provided that you
satisfy the following conditions:
You must retain the complete text of this software license in redistributions of
the TS-UNB-Lib software or your modifications thereto in source code form.
You must retain the complete text of this software license in the documentation
and/or other materials provided with redistributions of the TS-UNB-Lib software or
your modifications thereto in binary form. 
You must make available free of charge copies of the complete source code of the 
TS-UNB-Lib software and your modifications thereto to recipients of copies in binary form.
The name of Fraunhofer may not be used to endorse or promote products derived
from this software without prior written permission.
You may not charge copyright license fees for anyone to use, copy or distribute
the TS-UNB-Lib software or your modifications thereto.
Your modified versions of the TS-UNB-Lib software must carry prominent notices stating
that you changed the software and the date of any change. 
For modified versions of the TS-UNB-Lib software, the term "Fraunhofer TS-UNB-Lib"
must be replaced by the term "Third-Party Modified Version of the Fraunhofer TS-UNB-Lib."

3. NO PATENT LICENSE

NO EXPRESS OR IMPLIED LICENSES TO ANY PATENT CLAIMS, including without
limitation the patents of Fraunhofer, ARE GRANTED BY THIS SOFTWARE LICENSE.
Fraunhofer provides no warranty of patent non-infringement with respect to this
software.
You may use this TS-UNB-Lib software or modifications thereto only for
purposes that are authorized by appropriate patent licenses.

4. DISCLAIMER

This TS-UNB-Lib software is provided by Fraunhofer on behalf of the copyright
holders and contributors "AS IS" and WITHOUT ANY EXPRESS OR IMPLIED WARRANTIES,
including but not limited to the implied warranties of merchantability and
fitness for a particular purpose. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
CONTRIBUTORS BE LIABLE for any direct, indirect, incidental, special, exemplary,
or consequential damages, including but not limited to procurement of substitute
goods or services; loss of use, data, or profits, or business interruption,
however caused and on any theory of liability, whether in contract, strict
liability, or tort (including negligence), arising in any way out of the use of
this software, even if advised of the possibility of such damage.

5. CONTACT INFORMATION

Fraunhofer Institute for Integrated Circuits IIS  
Attention: Division Communication Systems  
Am Wolfsmantel 33  
91058 Erlangen, Germany  
ks-contracts@iis.fraunhofer.de  

----------------------------------------------------------------------------- */

/**
 * @brief Implementation of the TS-UNB variable uplink MAC
 *
 * @authors	Joerg Robert
 * @file	VariableMac.h
 *
 */


#ifndef TS_UNB_VARIABLE_UPLINK_MAC_H_
#define TS_UNB_VARIABLE_UPLINK_MAC_H_

#include <inttypes.h>
#include <stdint.h>

#ifdef __AVR_ARCH__
#include <avr/pgmspace.h>
#endif


#include "../Utils/BitAccess.h"


namespace TsUnbLib {
namespace TsUnb {



/**
 * @brief	Implementation of TS-UNB Variable Uplink MAC
 *
 * This class implements the TS-UNB Fixed Uplink MAC as defined in 6.3.3
 *
 */
template<uint8_t MAC_TYPE=3>
class VariableUplinkMac {
public:
	VariableUplinkMac () {
		extPkgCnt = 0;
	}

	/**
	 * @brief	Init method, must be called before usingthe other methods
	 * 
	 * @return	Error code, 0 on success
	 */
	int16_t init() {
		return 0;
	}

	/**
	 * @brief	Create MPDU payload out of MAC payload
	 * 
	 * @param	mpduPayload	Pointer to existing array for storing the output data (length at least MPDU_Length)
	 * @param	macPayload	Pointer to input MAC payload
	 * @param	len			Length of MAC payload data
	 * 
	 * @return	Lenth of MPDU payload 
	 */
	uint16_t encode(uint8_t* const mpduPayload, const uint8_t* const macPayload, const uint16_t len, 
	        const bool, const uint8_t) {
		mpduPayload[0] = MAC_TYPE;
		for (uint16_t i = 0; i < len; i++) {
			mpduPayload[i + 1] = macPayload[i];
		}
		extPkgCnt++;
		return len + 1;

	}

	/**
	 * @brief	Get the MPDU length for MAC_PayloadLength
	 * 
	 * @param	MAC_PayloadLength	Payload length of the MAC
	 * 
	 * @return	Lenght of the MPDU
	 * 
	 */
	uint16_t MPDU_Length(const uint16_t MAC_PayloadLength, const bool) const {
		return MAC_PayloadLength + 1;
	}
	
	/**
	 * @brief	Get LSB of Short Address for Sync Burst 
	 * 
	 * @return	0, as not used here 
	 * 
	 */
	uint8_t getLsbShortAddress() const {
		return 0; 
	}
	
	/**
	 * @brief	Get internal Counter (i.e. extPkgCnt)
	 * 
	 * @return	extPkgCnt
	 * 
	 */
	uint32_t getCounter() const {
		return extPkgCnt;
	}	

	uint32_t extPkgCnt;             /**< @brief Extended packet counter */
	static const uint8_t MMODE = 1;	/**< @brief MMODE for variable MAC */

};

};	// namespace TsUnb
};	// namespace TsUnbLib

#endif // TS_UNB_VARIABLE_UPLINK_MAC_H_

