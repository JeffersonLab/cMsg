/*----------------------------------------------------------------------------*
 *
 *  Copyright (c) 2005        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    E.Wolin, 18-Feb-2005, Jefferson Lab                                     *
 *                                                                            *
 *    Authors: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*
 *
 * Description:
 *
 *  String pattern matching using regular expressions used to match subject
 *  and type subscriptions with a message's subject and type.
 *
 *
 *----------------------------------------------------------------------------*/

/**
 * @file
 * This file contains the cMsg domain implementation of the cMsg user API.
 * This a messaging system programmed by the Data Acquisition Group at Jefferson
 * Lab. The cMsg domain has a dual function. It acts as a framework so that the
 * cMsg client can connect to a variety of subdomains (messaging systems). However,
 * it also acts as a messaging system itself in the cMsg <b>subdomain</b>.
 */  
 
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "regex.h"

/**
 * Characters which need to be escaped or replaced to avoid special interpretation
 * in regular expressions.
 */
static char *escapeChars = "\\(){}[]+.|^$*?";

/** Array of strings to replace the special characters with. */
static char *replaceWith[] = {"\\\\", "\\(", "\\)", "\\{", "\\}", "\\[","\\]",
                              "\\+" ,"\\.", "\\|", "\\^", "\\$", ".*", ".{1}"};

/**
 * This routine takes a string and escapes most special, regular expression characters.
 * The return string allows only * and ? to be passed through in a way meaningful
 * to regular expressions (as .* and .{1} respectively). The returned string 
 * is allocated memory which must be freed by the caller.
 *
 * @param s string to be escaped
 * @return escaped string
 */
char *cMsgStringEscape(const char *s) {
    int i, len, subIndex=0;
    char *c, *sub, catString[2];

    if (s == NULL) return NULL;

    /* First a quick test. Are there any chars in s that need escaping/replacing? */
    c = strpbrk(s, escapeChars);
    /* Nothing there so return. */
    if (c == NULL) {
        return (char *)strdup(s);
    }

    /* There are chars that need to be escaped and/or chars
     * that need to be replaced by a string, so
     * place characters one-by-one into a new string.
     * Add the "\" character in front of all characters
     * needing to be escaped, and replace those that need
     * replacing.
     * We also need to add a "^" to the front and a "$" to
     * the end for proper regular expression pattern matching.
     */

    /* Make string long enough to hold 4x original
     * string + 2 for beginning ^ and ending $ + 1 for ending null.
     */
    len = strlen(s);
    sub = (char *) malloc(4*len + 3);
    if (sub == NULL) return NULL;

    /* init strings */
    sub[0] = '^';
    sub[1] = '\0';
    catString[1] = '\0';

    for (i=0; i < len; i++) {
        /* Is this s character one to be escaped/replaced? */
        c = strchr(escapeChars, s[i]);
        /* If yes ... */
        if (c != NULL) {
            strcat(sub, replaceWith[c - escapeChars]);
        }
        /* If no, just add char */
        else {
            catString[0] = s[i];
            strcat(sub, catString);
        }
    }
    
    /* add "$" to end */
    len = strlen(sub);
    sub[len] = '$';
    sub[len+1] = '\0';
    
    return sub;
}



/**
 * This routine implements a simple wildcard matching scheme where "*" means
 * any or no characters and "?" means exactly 1 character.
 *
 * @param regexp subscription string that can contain the wildcards * and ?
 * @param s message string to be matched (can be blank which only matches *)
 * @return 1 if there is a match, 0 if there is not, -1 if there is an error condition
 */
int cMsgStringMatches(char *regexp, const char *s) {
    char *escapedString;
    int err,returnCode;
    regex_t re;

    /* Check args */
    if ((regexp == NULL)||(s == NULL)) return -1;

    /*
     * The first order of business is to take the regexp arg and modify it so that it is
     * a regular expression that the regex package can understand. This means subbing all
     * occurrences of "*" and "?" with ".*" and ".{1}". And it means escaping other regular
     * expression special characters.
    */
    escapedString = cMsgStringEscape(regexp);

    /* Now see if there's a match with the "s" arg */
    err = regcomp(&re, escapedString, REG_EXTENDED);
    if (err != 0) {
        /* printf("Unsuccessful compiling of %s\n", regexp);*/
        free(escapedString);
        return -1;
    }

    err = regexec(&re, s, 0, NULL, 0);
    if (err == 0) {
        returnCode = 1;
    }
    else if (err == REG_NOMATCH) {
        returnCode = 0;
    }
    else {
        returnCode = -1;
    }
    
    /* free up memory */
    free(escapedString);
    regfree(&re);
    
    return returnCode;
}


/**
 * This routine implements a simple wildcard matching scheme where "*" means
 * any or no characters and "?" means exactly 1 character. It is more efficient
 * than cMsgStringMatches as the first argument is assumed to have had
 * cMsgStringEscape already called on it (which cMsgStringMatches always does).
 *
 * @param regexp subscription string that has already had all regular expression
 *               symbols escaped (except * and ? which must be replaced with .* and .{1}).
 *               In other words, this argument must be a string returned by cMsgStringEscape.
 * @param s message string to be matched (can be blank which only matches *)
 * @return 1 if there is a match, 0 if there is not, -1 if there is an error condition
 */
int cMsgRegexpMatches(char *regexp, const char *s) {
    int err,returnCode;
    regex_t re;

    /* Check args */
    if ((regexp == NULL)||(s == NULL)) return -1;

    /* Now see if there's a match with the "s" arg */
    err = regcomp(&re, regexp, REG_EXTENDED);
    if (err != 0) {
        /* printf("Unsuccessful compiling of %s\n", regexp);*/
        return -1;
    }

    err = regexec(&re, s, 0, NULL, 0);
    if (err == 0) {
        returnCode = 1;
    }
    else if (err == REG_NOMATCH) {
        returnCode = 0;
    }
    else {
        returnCode = -1;
    }
    
    /* free up memory */
    regfree(&re);
    
    return returnCode;
}

