/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 8-Jul-2004, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/
#include <stdio.h>
#include <string.h>

#include "cMsgDomain.h"
#include "cMsgNetwork.h"


/******************************************************************/
int main(int argc,char **argv) {

    int        i, err, error, Port, index;
    int        dbg=1, mustMulticast = 0;
    size_t     len, bufLength, subRemainderLen;
    char       *p, *udl, *udlLowerCase, *udlRemainder, *remain;
    char       *buffer;
    const char *pattern = "([^:/]+):?([0-9]+)?/?([a-zA-Z0-9]+)?/?(.*)";
    regmatch_t matches[5]; /* we have 5 potential matches: 1 whole, 4 sub */
    regex_t    compiled;

    parsedUDL Udl;
    parsedUDL *pUdl = &Udl;

//    const char *UDL = "cmsg:cmsg://host:23456/";
//    const char *UDL =  "cmsg:cmsg://host:23456/NS/";
//    const char *UDL =  "cmsg:cmsg://host:23456/NS/?junk";
//    const char *UDL =  "cmsg:cmsg://host:23456/?junk";
//    const char *UDL =  "cmsg:cmsg://host:23456/cMsg/NS/?junk";
//    const char *UDL =  "cMsg:cMsg://host:23456/Database?driver=myDriver&url=myURL&";
    const char *UDL =  "cMsg:cMsg://multicast:23456/cMsg/NS?cmsgpassword=blah&subnet=129.57.29.255";

    if(dbg) printf("\n\nparseUDL: UDL = %s\n", UDL);
    if (UDL == NULL || pUdl == NULL) {
        return (CMSG_BAD_ARGUMENT);
    }

    /* make a copy */
    udl = strdup(UDL);

    /* make a copy in all lower case */
    udlLowerCase = strdup(UDL);
    len = strlen(udlLowerCase);
    for (i=0; i<len; i++) {
        udlLowerCase[i] = tolower(udlLowerCase[i]);
    }

    /* strip off the beginning cMsg:cMsg:// */
    p = strstr(udlLowerCase, "cmsg://");
    if (p == NULL) {
        free(udl);
        free(udlLowerCase);
        return(CMSG_BAD_ARGUMENT);
    }
    index = (int) (p - udlLowerCase);
    free(udlLowerCase);

    udlRemainder = udl + index + 7;
    if(dbg) printf("parseUDL: udl remainder = %s\n", udlRemainder);

    pUdl->udlRemainder = strdup(udlRemainder);

    /* make a big enough buffer to construct various strings, 256 chars minimum */
    len       = strlen(udlRemainder) + 1;
    bufLength = len < 256 ? 256 : len;
    buffer    = (char *) malloc(bufLength);
    if (buffer == NULL) {
        free(udl);
        return(CMSG_OUT_OF_MEMORY);
    }

    /* compile regular expression */
    err = cMsgRegcomp(&compiled, pattern, REG_EXTENDED);
    /* will never happen */
    if (err != 0) {
        free(udl);
        free(buffer);
        return (CMSG_ERROR);
    }

    /* find matches */
    err = cMsgRegexec(&compiled, udlRemainder, 5, matches, 0);
    if (err != 0) {
        /* no match */
        free(udl);
        free(buffer);
        return (CMSG_BAD_FORMAT);
    }

    /* free up memory */
    cMsgRegfree(&compiled);

    /* find host name */
    if (matches[1].rm_so < 0) {
        /* no match for host */
        free(udl);
        free(buffer);
        return (CMSG_BAD_FORMAT);
    }
    else {
        buffer[0] = 0;
        len = matches[1].rm_eo - matches[1].rm_so;
        strncat(buffer, udlRemainder+matches[1].rm_so, len);
        /* assume connected server is not local, set it properly after connection */
        pUdl->isLocal = 0;

        if (strcasecmp(buffer, "multicast") == 0 ||
            strcmp(buffer, CMSG_MULTICAST_ADDR) == 0) {
            mustMulticast = 1;
            if(dbg) printf("set mustMulticast to true (locally in parse method)\n");
        }
            /* if the host is "localhost", find the actual host name */
        else if (strcasecmp(buffer, "localhost") == 0) {
            if(dbg) printf("parseUDL: host = localhost\n");
            /* get canonical local host name */
            if (cMsgNetLocalHost(buffer, bufLength) != CMSG_OK) {
                /* error */
                free(udl);
                free(buffer);
                return (CMSG_BAD_FORMAT);
            }
            pUdl->isLocal = 1;
        }
        else {
            if(dbg) printf("parseUDL: host = %s, test to see if it is local\n", buffer);
            if (cMsgNetNodeIsLocal(buffer, &pUdl->isLocal) != CMSG_OK) {
                /* Could not find the given host. One possible reason
                 * is that the fully qualified name was used but this host is now
                 * on a different (home?) network. Try using the unqualified name
                 * before declaring failure.
                 */
                char *pend;
                if ( (pend = strchr(buffer, '.')) != NULL) {
                    /* shorten up the string */
                    *pend = '\0';
                    if(dbg) printf("parseUDL: host = %s, test to see if unqualified host is local\n", buffer);
                    if (cMsgNetNodeIsLocal(buffer, &pUdl->isLocal) != CMSG_OK) {
                        /* error */
                        free(udl);
                        free(buffer);
                        return (CMSG_BAD_FORMAT);
                    }
                }
            }
        }

        pUdl->nameServerHost = (char *)strdup(buffer);
        pUdl->mustMulticast = mustMulticast;
    }

    if(dbg) printf("parseUDL: host = %s\n", buffer);
    if(dbg) printf("parseUDL: mustMulticast = %d\n", mustMulticast);


    /* find port */
    if (matches[2].rm_so < 0) {
        /* no match for port so use default */
        if (mustMulticast == 1) {
            Port = CMSG_NAME_SERVER_MULTICAST_PORT;
        }
        else {
            Port = CMSG_NAME_SERVER_TCP_PORT;
        }
        if (cMsgDebug >= CMSG_DEBUG_WARN) {
            fprintf(stderr, "parseUDL: guessing that the name server port is %d\n",
                    Port);
        }
    }
    else {
        buffer[0] = 0;
        len = matches[2].rm_eo - matches[2].rm_so;
        strncat(buffer, udlRemainder+matches[2].rm_so, len);
        Port = atoi(buffer);
    }

    if (Port < 1024 || Port > 65535) {
        free(udl);
        free(buffer);
        return (CMSG_OUT_OF_RANGE);
    }

    if (mustMulticast == 1) {
        pUdl->nameServerUdpPort = Port;
        if(dbg) printf("parseUDL: UDP port = %hu\n", Port );
    }
    else {
        pUdl->nameServerPort = Port;
        if(dbg) printf("parseUDL: TCP port = %hu\n", Port );
    }


    /* find subdomain remainder */
    buffer[0] = 0;
    if (matches[4].rm_so < 0) {
        /* no match */
        pUdl->subRemainder = NULL;
        subRemainderLen = 0;
    }
    else {
        len = matches[4].rm_eo - matches[4].rm_so;
        subRemainderLen = len;
        strncat(buffer, udlRemainder+matches[4].rm_so, len);
        pUdl->subRemainder = strdup(buffer);
    }


    /* find subdomain */
    if (matches[3].rm_so < 0) {
        /* no match for subdomain, cMsg is default */
        pUdl->subdomain = strdup("cMsg");
        if(dbg) printf("parseUDL: no subdomain found in UDL\n");
    }
    else {
        /* All recognized subdomains */
        char *allowedSubdomains[] = {"LogFile", "CA", "Database",
                                     "Queue", "FileQueue", "SmartSockets",
                                     "TcpServer", "cMsg"};
        int j, foundSubD = 0;

        buffer[0] = 0;
        len = matches[3].rm_eo - matches[3].rm_so;
        strncat(buffer, udlRemainder+matches[3].rm_so, len);

        /*
         * Make sure the sub domain is recognized.
         * Because the cMsg subdomain is the only one in which a "/" is contained
         * in the remainder, and because the presence of the "cMsg" subdomain identifier
         * is optional, what will happen when it's parsed is that the namespace will be
         * interpreted as the subdomain if "cMsg" domain identifier is not there.
         * Thus we must take care of this case. If we don't recognize the subdomain,
         * assume it's the namespace of the cMsg subdomain.
         */
        for (j=0; j < 8; j++) {
            if (strcasecmp(allowedSubdomains[j], buffer) == 0) {
                foundSubD = 1;
                break;
            }
        }

        if (!foundSubD) {
            /* If here, sudomain is actually namespace and should
             * be part of subRemainder and subdomain is "cMsg" */
            if (pUdl->subRemainder == NULL || strlen(pUdl->subRemainder) < 1) {
                pUdl->subRemainder = strdup(buffer);
                if(dbg) printf("parseUDL: remainder null (or len 0) but set to %s\n",  pUdl->subRemainder);
            }
            else  {
                char *oldSubRemainder = pUdl->subRemainder;
                char *newRemainder = (char *)calloc(1, (len + subRemainderLen + 2));
                if (newRemainder == NULL) {
                    free(udl);
                    free(buffer);
                    return(CMSG_OUT_OF_MEMORY);
                }
                sprintf(newRemainder, "%s/%s", buffer, oldSubRemainder);
                pUdl->subRemainder = newRemainder;
                if(dbg) printf("parseUDL: remainder originally = %s, now = %s\n",oldSubRemainder, newRemainder );
                free(oldSubRemainder);
            }

            pUdl->subdomain = strdup("cMsg");
        }
        else {
            if(dbg) printf("parseUDL: found subdomain match found\n");
            pUdl->subdomain = strdup(buffer);
        }

    }
    if(dbg) printf("parseUDL: subdomain = %s\n", pUdl->subdomain);
    if(dbg) printf("parseUDL: subdomain remainder = %s\n",pUdl->subRemainder);

    /* find optional parameters */
    error = CMSG_OK;

    /* The while-loop doesn't loop and is just used to exit on error.
     * Only go in if subdomain remainder exists. */
    while (pUdl->subRemainder != NULL && strlen(pUdl->subRemainder) > 0) {
        /* find cmsgpassword parameter if it exists*/
        /* look for cmsgpassword=<value> */
        pattern = "[&\\?]cmsgpassword=([^&]+)";

        /* compile regular expression */
        err = cMsgRegcomp(&compiled, pattern, REG_EXTENDED | REG_ICASE);
        if (err != 0) {
            break;
        }

        /* this is the udl remainder in which we look */
        remain = strdup(pUdl->subRemainder);

        /* find matches */
        err = cMsgRegexec(&compiled, remain, 2, matches, 0);
        /* if match find (first) password */
        if (err == 0 && matches[1].rm_so >= 0) {
            int pos;
            buffer[0] = 0;
            len = matches[1].rm_eo - matches[1].rm_so;
            pos = matches[1].rm_eo;
            strncat(buffer, remain+matches[1].rm_so, len);
            pUdl->password = strdup(buffer);
            if(dbg) printf("parseUDL: password 1 = %s\n", buffer);

            /* see if there is another password defined (a no-no) */
            err = cMsgRegexec(&compiled, remain+pos, 2, matches, 0);
            if (err == 0 && matches[1].rm_so >= 0) {
                if(dbg) printf("Found duplicate password in UDL\n");
                /* there is another password defined, return an error */
                cMsgRegfree(&compiled);
                free(remain);
                error = CMSG_BAD_FORMAT;
                break;
            }

        }

        /* free up memory */
        cMsgRegfree(&compiled);

        /* find multicast timeout parameter if it exists */
        /* look for multicastTO=<value> */
        pattern = "[&\\?]multicastTO=([^&]+)";

        err = cMsgRegcomp(&compiled, pattern, REG_EXTENDED | REG_ICASE);
        if (err != 0) {
            free(remain);
            break;
        }

        err = cMsgRegexec(&compiled, remain, 2, matches, 0);
        /* if match find timeout */
        if (err == 0 && matches[1].rm_so >= 0) {
            int pos;
            buffer[0] = 0;
            len = matches[1].rm_eo - matches[1].rm_so;
            pos = matches[1].rm_eo;
            strncat(buffer, remain+matches[1].rm_so, len);

            /* Since atoi doesn't catch errors, we must check to
             * see if any char is a not a number. */
            for (i=0; i<len; i++) {
                if (!isdigit(buffer[i])) {
                    if(dbg) printf("Got nondigit in timeout = %c\n",buffer[i]);
                    cMsgRegfree(&compiled);
                    free(remain);
                    error = CMSG_BAD_FORMAT;
                    break;
                }
            }

            pUdl->timeout = atoi(buffer);
            if (pUdl->timeout < 0) {
                cMsgRegfree(&compiled);
                free(remain);
                error = CMSG_BAD_FORMAT;
                break;
            }
            if(dbg) printf("parseUDL: timeout = %d seconds\n", pUdl->timeout);

            /* see if there is another timeout defined (a no-no) */
            err = cMsgRegexec(&compiled, remain+pos, 2, matches, 0);
            if (err == 0 && matches[1].rm_so >= 0) {
                if(dbg) printf("Found duplicate timeout in UDL\n");
                /* there is another timeout defined, return an error */
                cMsgRegfree(&compiled);
                free(remain);
                error = CMSG_BAD_FORMAT;
                break;
            }
        }

        cMsgRegfree(&compiled);

        /* find regime parameter if it exists */
        /* look for regime=<value> */
        pattern = "[&\\?]regime=([^&]+)";

        err = cMsgRegcomp(&compiled, pattern, REG_EXTENDED | REG_ICASE);
        if (err != 0) {
            free(remain);
            break;
        }

        err = cMsgRegexec(&compiled, remain, 2, matches, 0);
        /* if match find regime */
        if (err == 0 && matches[1].rm_so >= 0) {
            int pos;
            buffer[0] = 0;
            len = matches[1].rm_eo - matches[1].rm_so;
            pos = matches[1].rm_eo;
            strncat(buffer, remain+matches[1].rm_so, len);
            if (strcasecmp(buffer, "low") == 0) {
                pUdl->regime = CMSG_REGIME_LOW;
                if(dbg) printf("parseUDL: regime = low\n");
            }
            else if (strcasecmp(buffer, "high") == 0) {
                pUdl->regime = CMSG_REGIME_HIGH;
                if(dbg) printf("parseUDL: regime = high\n");
            }
            else if (strcasecmp(buffer, "medium") == 0) {
                pUdl->regime = CMSG_REGIME_MEDIUM;
                if(dbg) printf("parseUDL: regime = medium\n");
            }
            else {
                if(dbg) printf("parseUDL: regime = %s, return error\n", buffer);
                cMsgRegfree(&compiled);
                free(remain);
                error = CMSG_BAD_FORMAT;
                break;
            }

            /* see if there is another regime defined (a no-no) */
            err = cMsgRegexec(&compiled, remain+pos, 2, matches, 0);
            if (err == 0 && matches[1].rm_so >= 0) {
                if(dbg) printf("Found duplicate regime in UDL\n");
                /* there is another timeout defined, return an error */
                cMsgRegfree(&compiled);
                free(remain);
                error = CMSG_BAD_FORMAT;
                break;
            }

        }


        cMsgRegfree(&compiled);

        /* find failover parameter if it exists */
        /* look for failover=<value> */
        pattern = "[&\\?]failover=([^&]+)";

        err = cMsgRegcomp(&compiled, pattern, REG_EXTENDED | REG_ICASE);
        if (err != 0) {
            free(remain);
            break;
        }

        err = cMsgRegexec(&compiled, remain, 2, matches, 0);
        /* if match find failover */
        if (err == 0 && matches[1].rm_so >= 0) {
            int pos;
            buffer[0] = 0;
            len = matches[1].rm_eo - matches[1].rm_so;
            pos = matches[1].rm_eo;
            strncat(buffer, remain+matches[1].rm_so, len);
            if (strcasecmp(buffer, "any") == 0) {
                pUdl->failover = CMSG_FAILOVER_ANY;
                if(dbg) printf("parseUDL: failover = any\n");
            }
            else if (strcasecmp(buffer, "cloud") == 0) {
                pUdl->failover = CMSG_FAILOVER_CLOUD;
                if(dbg) printf("parseUDL: failover = cloud\n");
            }
            else if (strcasecmp(buffer, "cloudonly") == 0) {
                pUdl->failover = CMSG_FAILOVER_CLOUD_ONLY;
                if(dbg) printf("parseUDL: failover = cloud only\n");
            }
            else {
                if(dbg) printf("parseUDL: failover = %s, return error\n", buffer);
                cMsgRegfree(&compiled);
                free(remain);
                error = CMSG_BAD_FORMAT;
                break;
            }

            /* see if there is another failover defined (a no-no) */
            err = cMsgRegexec(&compiled, remain+pos, 2, matches, 0);
            if (err == 0 && matches[1].rm_so >= 0) {
                if(dbg) printf("Found duplicate failover in UDL\n");
                /* there is another failover defined, return an error */
                cMsgRegfree(&compiled);
                free(remain);
                error = CMSG_BAD_FORMAT;
                break;
            }

        }


        cMsgRegfree(&compiled);

        /* find failover parameter if it exists */
        /* look for cloud=<value> */
        pattern = "[&\\?]cloud=([^&]+)";

        err = cMsgRegcomp(&compiled, pattern, REG_EXTENDED | REG_ICASE);
        if (err != 0) {
            free(remain);
            break;
        }

        err = cMsgRegexec(&compiled, remain, 2, matches, 0);
        /* if match find cloud */
        if (err == 0 && matches[1].rm_so >= 0) {
            int pos;
            buffer[0] = 0;
            len = matches[1].rm_eo - matches[1].rm_so;
            pos = matches[1].rm_eo;
            strncat(buffer, remain+matches[1].rm_so, len);
            if (strcasecmp(buffer, "any") == 0) {
                pUdl->cloud = CMSG_CLOUD_ANY;
                if(dbg) printf("parseUDL: cloud = any\n");
            }
            else if (strcasecmp(buffer, "local") == 0) {
                pUdl->cloud = CMSG_CLOUD_LOCAL;
                if(dbg) printf("parseUDL: cloud = local\n");
            }
            else {
                if(dbg) printf("parseUDL: cloud = %s, return error\n", buffer);
                cMsgRegfree(&compiled);
                free(remain);
                error = CMSG_BAD_FORMAT;
                break;
            }

            /* see if there is another failover defined (a no-no) */
            err = cMsgRegexec(&compiled, remain+pos, 2, matches, 0);
            if (err == 0 && matches[1].rm_so >= 0) {
                if(dbg) printf("Found duplicate cloud in UDL\n");
                /* there is another cloud defined, return an error */
                cMsgRegfree(&compiled);
                free(remain);
                error = CMSG_BAD_FORMAT;
                break;
            }

        }


        /* free up memory */
        cMsgRegfree(&compiled);

        /* find domain server port parameter if it exists */
        /* look for domainPort=<value> */
        pattern = "[&\\?]domainPort=([^&]+)";

        err = cMsgRegcomp(&compiled, pattern, REG_EXTENDED | REG_ICASE);
        if (err != 0) {
            free(remain);
            break;
        }

        err = cMsgRegexec(&compiled, remain, 2, matches, 0);
        /* if match find port */
        if (err == 0 && matches[1].rm_so >= 0) {
            int pos;
            buffer[0] = 0;
            len = matches[1].rm_eo - matches[1].rm_so;
            pos = matches[1].rm_eo;
            strncat(buffer, remain+matches[1].rm_so, len);

            /* Since atoi doesn't catch errors, we must check to
             * see if any char is a not a number. */
            for (i=0; i<len; i++) {
                if (!isdigit(buffer[i])) {
                    if(dbg) printf("Got nondigit in port = %c\n",buffer[i]);
                    cMsgRegfree(&compiled);
                    free(remain);
                    error = CMSG_BAD_FORMAT;
                    break;
                }
            }

            pUdl->domainServerPort = atoi(buffer);
            if (pUdl->domainServerPort < 1024 || pUdl->domainServerPort > 65535) {
                cMsgRegfree(&compiled);
                free(remain);
                error = CMSG_BAD_FORMAT;
                break;
            }
            if(dbg) printf("parseUDL: domain server port = %d\n", pUdl->domainServerPort);

            /* see if there is another domain server port defined (a no-no) */
            err = cMsgRegexec(&compiled, remain+pos, 2, matches, 0);
            if (err == 0 && matches[1].rm_so >= 0) {
                if(dbg) printf("Found duplicate domain server port in UDL\n");
                /* there is another domain server port defined, return an error */
                cMsgRegfree(&compiled);
                free(remain);
                error = CMSG_BAD_FORMAT;
                break;
            }
        }


        /* free up memory */
        cMsgRegfree(&compiled);

        /* find preferred subnet parameter if it exists, */
        /* look for subnet=<value> */
        pattern = "[&\\?]subnet=([^&]+)";

        /* compile regular expression */
        err = cMsgRegcomp(&compiled, pattern, REG_EXTENDED | REG_ICASE);
        if (err != 0) {
            free(remain);
            break;
        }

        /* find matches */
        err = cMsgRegexec(&compiled, remain, 2, matches, 0);
        /* if match find (first) subnet */
        if (err == 0 && matches[1].rm_so >= 0) {
            char *subnet = NULL;
            int pos;
            buffer[0] = 0;
            len = matches[1].rm_eo - matches[1].rm_so;
            pos = matches[1].rm_eo;
            strncat(buffer, remain+matches[1].rm_so, len);
            if(dbg) printf("parseUDL: subnet = %s\n", buffer);

            /* check to make sure it really is a local subnet */
            cMsgNetGetBroadcastAddress(buffer, &subnet);
            if(dbg) printf("parseUDL: output of cMsgNetGetBroadcastAddress = %s\n", subnet);

            /* if it is NOT a local subnet, forget about it */
            if (subnet != NULL) {
                pUdl->subnet = subnet;
            }
        }
        else {
            if(dbg) printf("parseUDL: no stinking match for subnet\n");
        }

        /* free up memory */
        cMsgRegfree(&compiled);
        free(remain);
        break;
    }


    /* UDL parsed ok */
    if(dbg) printf("DONE PARSING UDL\n");
    free(udl);
    free(buffer);

    return(0);
}
