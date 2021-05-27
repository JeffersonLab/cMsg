/**
 * Copyright (c) 2021, Jefferson Science Associates
 *
 * Thomas Jefferson National Accelerator Facility
 * Data Acquisition Group
 *
 * 12000, Jefferson Ave, Newport News, VA 23606
 * Phone : (757)-269-7100
 *
 * @date 05/27/2021
 * @author timmer
 */

/**
 * @file
 * This is the header file for the emu domain implementation of cMsg.
 */

#ifndef CMSG6_0_EMUDOMAIN_H
#define CMSG6_0_EMUDOMAIN_H

#ifdef	__cplusplus
extern "C" {
#endif


void setDirectConnectDestination(const char **ip, const char **broad, int count);



#ifdef	__cplusplus
}
#endif

#endif
