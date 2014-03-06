/*----------------------------------------------------------------------------*
 *
 *  Copyright (c) 2005        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 18-Feb-2005, Jefferson Lab                                   *
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
 * This file contains part of the cMsg domain implementation of the cMsg messaging
 * system -- the routines which do regular expression matching.</b>.
 */  
 
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "cMsgRegex.h"
#include "cMsgDomain.h"

/**
 * Characters which need to be escaped or replaced to avoid special interpretation
 * in regular expressions.
 */
static const char *RegexpChars = "\\(){}[]+.|^$";

/**
 * Characters which need to be escaped or replaced to avoid special interpretation
 * in regular expressions (except the bar, |).
 */
static const char *RegexpNoBar = "\\(){}[]+.^$";

/**
 * Characters (only bar) which needs to be escaped to avoid special interpretation
 * in regular expressions.
 */
static const char *RegexpBar = "|";

/**
 * Pseudo wildcard characters which need to be replaced with valid regular expressions.
 */
static const char *PseudoWildcards = "*?#";

/** Array of strings to replace the special characters with. */
static const char *replaceRegexp[] = {"\\\\", "\\(", "\\)", "\\{", "\\}", "\\[","\\]",
                                      "\\+" ,"\\.", "\\|", "\\^", "\\$"};

/** Array of strings to replace the special characters with (except the bar, |). */
static const char *replaceNoBar[] = {"\\\\", "\\(", "\\)", "\\{", "\\}", "\\[","\\]",
                                     "\\+" ,"\\.","\\^", "\\$"};

/** Array of strings (only escaped bar) to replace the bar with. */
static const char *replaceBar[] = {"\\|"};

/** Array of strings to replace the pseudo wildcard characters. */
static const char *replacePseudo[] = {".*", ".{1}", "[0-9]*"};

/**
 * A regular expression string that matches a single part of a subject or type
 * that describes a range of positive integers in a special syntax (described in the
 * doxygen doc that's a part of this class). The part is captured for further
 * regular expression analysis.
 */
static const char* exprRange = "\\\\{([ i<>=&|[0-9]*]*)\\\\}";

/**
 * A regular expression string that completely matches one part of a subject or type
 * describing a range of positive integers in a special syntax (described in the
 * javadoc that's a part of this class). This determines if the part's format is correct.
 */
static const char* exprFull = "(i|[0-9]+)[<>=](i|[0-9]+)([|&](i|[0-9]+)[<>=](i|[0-9]+))*";

/**
 * A regular expression string that matches sections of one part of a subject or type
 * describing a range of positive integers in a special syntax (described in the
 * javadoc that's a part of this class). This is designed to capture all information in
 * each section which is stored for use when matching a message's subject and type.
 */
static const char* exprSec  = "(i|[0-9]+)([<>=])(i|[0-9]+)([|&])*";

/** Int representation of < sign. */
static const int LT  = 0;
/** Int representation of > sign. */
static const int GT  = 1;
/** Int representation of = sign. */
static const int EQ  = 2;
/** Int representation of || sign. */
static const int OR  = 3;
/** Int representation of && sign. */
static const int AND = 4;


static char *stringToRegexp(const char *s, const char *replaceChars,
                            const char **replaceWith, int *replacementsDone);
static char *stringReplace(const char *s, const char *replaceChars,
                           const char **replaceWith, int *replacementsDone);
static int  rangeParse(char *subtyp, char **subRegexp, numberRange **subRange, int *rangeCount);
static int  regexpMatch(char *subtyp, regex_t *compiled,
                        numberRange *topHead, int rangeCount, int *match);
static int  setRegexpStuff(char *str, char **pRegexp, numberRange **pRange,
                           regex_t *compRegexp, int *rCount, int *wcCount);

static char *stringEscape(const char *s);
static char *stringEscapeBar(const char *s);
static char *stringEscapeNoBar(const char *s);
static char *stringEscapePseudo(const char *s, int *wildCardCount);


/**
 * This routine takes a string, replaces the given list of characters with a given
 * list of strings and returns a regular expression string. If no characters are
 * replaced, the only changes to the string will be the insertion of "^" at the 
 * beginning and "$" at the end. The returned string is allocated memory which must
 * be freed by the caller.
 *
 * @param s string to have characters replaced
 * @param replaceChars string of characters, each of which is to be replaced
 * @param replaceWith  array of strings each string of which replaces a corresponding
 *                     character in the replaceChars argument
 * @param replacementsDone pointer to int gets filled with number of replacements done
 * @return replaced string
 */
static char *stringToRegexp(const char *s, const char *replaceChars,
                            const char **replaceWith, int *replacementsDone) {
    int i, len, replacements=0;
    const char *c;
    char *sub, catString[2];

    if (s == NULL) return NULL;

    /* First a quick test. Are there any chars in s that need escaping/replacing? */
    c = strpbrk(s, replaceChars);
    /* Nothing there so just add ^ in front and $ on end and return. */
    if (c == NULL) {
        len = strlen(s);
        sub = (char *) calloc(1, len + 3);
        if (sub == NULL) return NULL;
        sub[0] = '^';
        strcat(sub, s);
        sub[len+1] = '$';
        return sub;
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
    sub = (char *) calloc(1, 4*len + 3);
    if (sub == NULL) return NULL;

    /* init strings */
    sub[0] = '^';
    catString[1] = '\0';

    for (i=0; i < len; i++) {
        /* Is this s character one to be escaped/replaced? */
        c = strchr(replaceChars, s[i]);
        /* If yes ... */
        if (c != NULL) {
            strcat(sub, replaceWith[c - replaceChars]);
            replacements++;
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
    
    if (replacementsDone != NULL) *replacementsDone = replacements;
    
    return sub;
}


/**
 * This routine takes a string, replaces the given list of characters with a given
 * list of strings and returns the resultant string. The returned string is
 * allocated memory which must be freed by the caller.
 *
 * @param s string to have characters replaced
 * @param replaceChars string of characters, each of which is to be replaced
 * @param replaceWith  array of strings each string of which replaces a corresponding
 *                     character in the replaceChars argument
 * @param replacementsDone pointer to int gets filled with number of replacements done
 * @return replaced string
 */
static char *stringReplace(const char *s, const char *replaceChars,
                           const char **replaceWith, int *replacementsDone) {
    int i, len, replacements=0;
    const char *c;
    char *sub, catString[2];

    if (s == NULL) return NULL;

    /* First a quick test. Are there any chars in s that need escaping/replacing? */
    c = strpbrk(s, replaceChars);
    /* Nothing there, return copy of s. */
    if (c == NULL) {
        return strdup(s);
    }

    /* There are chars that need to be replaced by a string, so
     * place strings one-by-one into a new string. */

    /* Make string long enough to hold 6x original string + 1 for ending null. */
    len = strlen(s);
    sub = (char *) malloc(6*len + 1);
    if (sub == NULL) return NULL;

    /* init strings */
    sub[0] = '\0';
    catString[1] = '\0';

    for (i=0; i < len; i++) {
        /* Is this s character one to be escaped/replaced? */
        c = strchr(replaceChars, s[i]);
        /* If yes ... */
        if (c != NULL) {
            strcat(sub, replaceWith[c - replaceChars]);
            replacements++;
        }
        /* If no, just add char */
        else {
            catString[0] = s[i];
            strcat(sub, catString);
        }
    }
        
    if (replacementsDone != NULL) *replacementsDone = replacements;
    
    return sub;
}


/**
 * This routine takes a string, escapes all regular expression characters and
 * replaces the pseudo wildcard characters *, ?, and #, with the valid regular
 * expressions .*, .{1}, and [0-9]* respectively. The returned string will have
 * the character "^" inserted at the beginning and the character inserted "$" at
 * the end. The returned string is allocated memory which must be freed by the
 * caller.
 *
 * @param s string to have characters replaced
 * @param wildCardCount pointer to int gets filled with number of wildcards
 *                      (*,? and # chars) that were found in s
 * @return NULL if s arg is NULL or out of memory
 * @return replaced string
 */
char *cMsgStringToRegexp(const char *s, int *wildCardCount) {
    char *str1, *str2;
    if (s == NULL) return NULL;
    
    /* escape regexp chars */
    str1 = stringToRegexp(s, RegexpChars, replaceRegexp, NULL);
    
    /* substitute for pseudo wildcards */
    str2 = stringEscapePseudo(str1, wildCardCount);
    
    return str2;
}

/**
 * This routine takes a string, escapes all regular expression characters and
 * returns a regular expression string. If no characters are escaped,
 * the only changes to the string will be the insertion of "^" at the 
 * beginning and "$" at the end. The returned string is allocated memory which must
 * be freed by the caller.
 * The returned string allows only *, ?, and # to be passed through.
 *
 * @param s string to have characters replaced
 * @param wildCardsInString pointer to int gets filled with number of wildcards
 *                          (*,? and # chars) that were found in s
 * @return replaced string
 */
static char *stringEscape(const char *s) {
    return stringToRegexp(s, RegexpChars, replaceRegexp, NULL);
}

/**
 * This routine takes a string, escapes all regular expression characters with the
 * exception of the bar character and returns a regular expression string.
 * If no characters are escaped, the only changes to the string will be the
 * insertion of "^" at the beginning and "$" at the end. The returned string
 * is allocated memory which must be freed by the caller.
 * The returned string allows only *, ?, and # to be passed through.
 *
 * @param s string to have characters replaced
 * @param wildCardsInString pointer to int gets filled with number of wildcards
 *                          (*,? and # chars) that were found in s
 * @return replaced string
 */
static char *stringEscapeNoBar(const char *s) {
    return stringToRegexp(s, RegexpNoBar, replaceNoBar, NULL);
}


/**
 * This routine takes a string, escapes all bar characters.
 *
 * @param s string to have characters replaced
 * @return replaced string
 */
static char *stringEscapeBar(const char *s) {
    return stringReplace(s, RegexpBar, replaceBar, NULL);
}


/**
 * This routine replaces the pseudo wildcard characters *, ?, and #,
 * with the valid regular expressions .*, .{1}, and [0-9]* respectively.
 *
 * @param s string to have characters replaced
 * @param wildCardCount pointer to int gets filled with number of pseudo wildcards
 *                      (*,? and # chars) that were found in s
 * @return replaced string
 */
static char *stringEscapePseudo(const char *s, int *wildCardCount) {
    return stringReplace(s, PseudoWildcards, replacePseudo, wildCardCount);
}



/**
 * This routine implements a simple wildcard matching scheme where "*" means
 * any or no characters, "?" means exactly 1 character, and "#" means one or no
 * positive integer.
 *
 * @param regexp subscription string that can contain the wildcards *, ?, and #
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
    escapedString = cMsgStringToRegexp(regexp, NULL);

    /* Now see if there's a match with the "s" arg */
    err = cMsgRegcomp(&re, escapedString, REG_EXTENDED);
    if (err != 0) {
        /* printf("Unsuccessful compiling of %s\n", regexp);*/
        free(escapedString);
        return -1;
    }

    err = cMsgRegexec(&re, s, 0, NULL, 0);
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
    cMsgRegfree(&re);
    
    return returnCode;
}


/**
 * This routine checks to see if there is a match between a (message's) subject & type
 * pair and a subscribeAndGet's subject and type.
 * 
 * If they have no wildcards characters, then a straight string compare is done.
 * If there are wildcard characters a match using regular expressions is done.
 * The subAndGet's subject and type may include wildcards where "*" means any or
 * no characters, "?" means exactly 1 character, "#" means 1 or no positive integer,
 * and a defined range (eg. {i>5 & i<10 | i=66}) matches 1 integer and applies the given
 * logic to see if there is a match with that integer.
 *
 * @param info pointer to subscribeAndGet structure
 * @param msgSubject (message's) subject
 * @param msgType (message's) type
 * @return 1 if there is a match of both subject and type, 0 if there is not (or error)
 */
int cMsgSubAndGetMatches(getInfo *info, char *msgSubject, char *msgType) {
    int err, match;
    subInfo sub;
    
    /* first check for null subjects/types in subscription */
    if (info->subject == NULL || info->type == NULL) return 0;
    
    /* use subscription structure as all routines are setup to use it */
    sub.subWildCardCount  = 0;
    sub.typeWildCardCount = 0;
    sub.subRangeCount     = 0;
    sub.typeRangeCount    = 0;
    sub.type              = info->type;
    sub.subject           = info->subject;
    sub.typeRegexp        = NULL;
    sub.subjectRegexp     = NULL;
    sub.subRange          = NULL;
    sub.typeRange         = NULL;

    err = cMsgSubscriptionSetRegexpStuff(&sub);
    if (err != CMSG_OK) {
        if (sub.typeRegexp)    free(sub.typeRegexp);
        if (sub.subjectRegexp) free(sub.subjectRegexp);
        cMsgNumberRangeFree(sub.subRange);
        cMsgNumberRangeFree(sub.typeRange);
        if (sub.subWildCardCount)  cMsgRegfree(&sub.compSubRegexp);
        if (sub.typeWildCardCount) cMsgRegfree(&sub.compTypeRegexp);
        return 0;
    }
    
    /* else if there are no wildcards in the subscription's subject, just use string compare */
    if (!sub.subWildCardCount) {
        if (strcmp(msgSubject, sub.subject) != 0) {
            if (sub.typeWildCardCount) {
                free(sub.typeRegexp);
                cMsgNumberRangeFree(sub.typeRange);
                cMsgRegfree(&sub.compTypeRegexp);
            }
            return 0;
        }
    }
    /* else if there are wildcards in the subscription's subject, use regexp matching */
    else {
        /* printf("Wildcards in subject, use regexps for matching\n"); */
        /* ignore errors since there are no bad args and
         * we'll just ignore out of memory for now. */
        regexpMatch(msgSubject, &sub.compSubRegexp,
                    sub.subRange, sub.subRangeCount, &match);
        cMsgRegfree(&sub.compSubRegexp);
        if (!match) {
            free(sub.subjectRegexp);
            cMsgNumberRangeFree(sub.subRange);
            if (sub.typeWildCardCount) {
                free(sub.typeRegexp);
                cMsgNumberRangeFree(sub.typeRange);
                cMsgRegfree(&sub.compTypeRegexp);
            }
            return 0;
        }
    }
    /* printf("Msg subject (%s) matches regexp (%s)\n", msgSubject, sub.subjectRegexp); */

    if (!sub.typeWildCardCount) {
        if (strcmp(msgType, sub.type) != 0) {
            if (sub.subWildCardCount) {
                free(sub.subjectRegexp);
                cMsgNumberRangeFree(sub.subRange);
                cMsgRegfree(&sub.compSubRegexp);
            }
            return 0;
        }
    }
    else {
        /* printf("Wildcards in type, use regexps for matching\n"); */
        regexpMatch(msgType, &sub.compTypeRegexp,
                    sub.typeRange, sub.typeRangeCount, &match);
        cMsgRegfree(&sub.compTypeRegexp);
        if (!match) {
            free(sub.typeRegexp);
            cMsgNumberRangeFree(sub.typeRange);
            if (sub.subWildCardCount) {
                free(sub.subjectRegexp);
                cMsgNumberRangeFree(sub.subRange);
                cMsgRegfree(&sub.compSubRegexp);
            }
            return 0;
        }
    }
    /* printf("Msg type (%s) matches regexp (%s)\n", msgType, sub.typeRegexp); */

    if (sub.typeRegexp)    free(sub.typeRegexp);
    if (sub.subjectRegexp) free(sub.subjectRegexp);
    cMsgNumberRangeFree(sub.subRange);
    cMsgNumberRangeFree(sub.typeRange);
    
    return 1;
}


/**
 * This routine checks to see if there is a match between a (message's) subject & type
 * pair and a subscription's subject and type. Hash tables are not used so a full check
 * is done each time this routine is called.
 * 
 * If they have no wildcards characters, then a straight string compare is done.
 * If there are wildcard characters a match using regular expressions is done.
 * The subscription's subject and type may include wildcards where "*" means any or
 * no characters, "?" means exactly 1 character, "#" means 1 or no positive integer,
 * and a defined range (eg. {i>5 & i<10 | i=66}) matches 1 integer and applies the given
 * logic to see if there is a match with that integer.
 *
 * @param sub pointer to subscription structure
 * @param msgSubject (message's) subject
 * @param msgType (message's) type
 * @return 1 if there is a match of both subject and type, 0 if there is not
 */
int cMsgSubscriptionMatchesNoHash(subInfo *sub, char *msgSubject, char *msgType) {
    int match;
    
    /* first check for null subjects/types in subscription */
    if (sub->subject == NULL || sub->type == NULL) return 0;

    /* else if there are no wildcards in the subscription's subject, just use string compare */
    if (!sub->subWildCardCount) {
        if (strcmp(msgSubject, sub->subject) != 0) return 0;
    }
    /* else if there are wildcards in the subscription's subject, use regexp matching */
    else {
        /* printf("Wildcards in subject, use regexps for matching\n"); */
        /* ignore errors since there are no bad args and
         * we'll just ignore out of memory for now. */
        regexpMatch(msgSubject, &sub->compSubRegexp,
                    sub->subRange, sub->subRangeCount, &match);
        if (!match) {
            return 0;
        }
    }
    /* printf("Msg subject (%s) matches regexp (%s)\n", msgSubject, sub->subjectRegexp); */

    if (!sub->typeWildCardCount) {
        if (strcmp(msgType, sub->type) != 0) return 0;
    }
    else {
        /* printf("Wildcards in type, use regexps for matching\n"); */
        regexpMatch(msgType, &sub->compTypeRegexp,
                    sub->typeRange, sub->typeRangeCount, &match);
        if (!match) {
            return 0;
        }
    }
    /* printf("Msg type (%s) matches regexp (%s)\n", msgType, sub->typeRegexp); */

    return 1;
}


/**
 * This routine checks to see if there is a match between a (message's) subject & type
 * pair and a subscription's subject and type.
 * First, the subject and type are checked to see if they are in hash tables of
 * known-to-match strings.
 * Second, if not and if they have no wildcards characters,
 * then a straight string compare is done.
 * Third, if there are wildcard characters a match using regular expressions is done.
 * The subscription's subject and type may include wildcards where "*" means any or
 * no characters, "?" means exactly 1 character, "#" means 1 or no positive integer,
 * and a defined range (eg. {i>5 & i<10 | i=66}) matches 1 integer and applies the given
 * logic to see if there is a match with that integer.
 *
 * @param sub pointer to subscription structure
 * @param msgSubject (message's) subject
 * @param msgType (message's) type
 * @return 1 if there is a match of both subject and type, 0 if there is not
 */
int cMsgSubscriptionMatches(subInfo *sub, char *msgSubject, char *msgType) {
    int match;
    
    /* first check for null subjects/types in subscription */
    if (sub->subject == NULL || sub->type == NULL) return 0;

    /* first see if it's stored in the set of strings known to match */
    if (!hashLookup(&sub->subjectTable, msgSubject, NULL)) {
        /* else if there are no wildcards in the subscription's subject, just use string compare */
        if (!sub->subWildCardCount) {
            if (strcmp(msgSubject, sub->subject) != 0) return 0;
        }
        /* else if there are wildcards in the subscription's subject, use regexp matching */
        else {
/* printf("Wildcards in subject, use regexps for matching\n"); */
            /* ignore errors since there are no bad args and
             * we'll just ignore out of memory for now. */
            regexpMatch(msgSubject, &sub->compSubRegexp,
                        sub->subRange, sub->subRangeCount, &match);
            if (!match) {
                return 0;
            }
        }
/* printf("Msg subject (%s) matches regexp (%s)\n", msgSubject, sub->subjectRegexp); */

        /* first check to see if our hash table is getting too big */
        if (hashSize(&sub->subjectTable) > 200) {
            /* clear things out */
            hashClear(&sub->subjectTable, NULL, NULL);
        }

        /* add to table since it matches */
        hashInsert(&sub->subjectTable, msgSubject, NULL, NULL);
    }
    /*
    else {
        printf("Msg subject (%s) matches - in hash table\n", msgSubject);
    }
    */

    if (!hashLookup(&sub->typeTable, msgType, NULL)) {
        if (!sub->typeWildCardCount) {
            if (strcmp(msgType, sub->type) != 0) return 0;
        }
        else {
/* printf("Wildcards in type, use regexps for matching\n"); */
            regexpMatch(msgType, &sub->compTypeRegexp,
                        sub->typeRange, sub->typeRangeCount, &match);
            if (!match) {
                return 0;
            }
        }
/* printf("Msg type (%s) matches regexp (%s)\n", msgType, sub->typeRegexp); */

        if (hashSize(&sub->typeTable) > 200) {
            hashClear(&sub->typeTable, NULL, NULL);
        }

        hashInsert(&sub->typeTable, msgType, NULL, NULL);
    }
    /*
    else {
        printf("Msg type (%s) matches - in hash table\n", msgType);
    }
    */
    
    return 1;
}


/**
 * Given a subscription, if its subject and/or type contains pseudo wildcards (*, ?, #, and {}),
 * this routine creates a valid regular expression string from that subject and/or type.
 * It also precompiles the regular expression and stores the number of wildcards found.
 * If valid number ranges are found, they are parsed and stored in the subscription structure.
 *
 * @param sub pointer to subscription structure
 * 
 * @return CMSG_OK            if everything OK
 * @return CMSG_ERROR         if cannot compile regular expression
 * @return CMSG_BAD_ARGUMENT  if sub argument or subject or type is NULL
 * @return CMSG_OUT_OF_MEMORY if out of memory
 */
int cMsgSubscriptionSetRegexpStuff(subInfo *sub) {
    int err;
    
    sub->subRangeCount     = 0;
    sub->typeRangeCount    = 0;
    sub->subWildCardCount  = 0;
    sub->typeWildCardCount = 0;

    err = setRegexpStuff(sub->subject, &sub->subjectRegexp, &sub->subRange,
                         &sub->compSubRegexp, &sub->subRangeCount,
                         &sub->subWildCardCount);
    if (err != CMSG_OK) return (err);
    
    err = setRegexpStuff(sub->type, &sub->typeRegexp, &sub->typeRange,
                         &sub->compTypeRegexp, &sub->typeRangeCount,
                         &sub->typeWildCardCount);
    if (err != CMSG_OK) return (err);
    
    return (CMSG_OK);
}


/**
 * Given a string (subject or type), if it contains pseudo wildcards (*, ?, #, and {}),
 * this routine creates a valid regular expression from that string.
 * It also precompiles the regular expression and finds the number of wildcards contained
 * in that string. If valid number ranges are found, they are parsed and returned.
 *
 * @param sub pointer to subscription structure
 * 
 * @return CMSG_OK            if everything OK
 * @return CMSG_ERROR         if cannot compile regular expression
 * @return CMSG_BAD_ARGUMENT  if sub argument or subject is NULL
 * @return CMSG_OUT_OF_MEMORY if out of memory
 */
static int setRegexpStuff(char *str, char **pRegexp, numberRange **pRange,
                          regex_t *compRegexp, int *rCount, int *wcCount) {
    char *c, *regexp=NULL; 
    numberRange *range=NULL;
    int err, wildCards=0, rangeCount=0, wildCardCount=0;
    
    if (str == NULL) return (CMSG_BAD_ARGUMENT);

    /* First a quick test. Are both "{" and "}" in subject? If so, look for ranges. */
    if (strpbrk(str, "{") != NULL && strpbrk(str, "}") != NULL) {
        /* 1) This does nothing if no ranges found.
         * 2) Otherwise it sets regexp to the subject
         *    with escaped regular expression characters if any,
         *    and also substitutes for number range instances.
         *    This will set rangeCount to the number
         *    of valid ranges.
         */
        err = rangeParse(str, &regexp, &range, &rangeCount);
        if (err != CMSG_OK) {
            return (err);
        }
        wildCardCount = rangeCount;
    }

    /* we only need to do the regexp stuff if there are pseudo wildcards chars in subject */
    if (strpbrk(str, PseudoWildcards) != NULL) {
        /* if no valid ranges were found ... */
        if (!rangeCount) {
             regexp = stringToRegexp(str, PseudoWildcards, replacePseudo, &wildCards);
             wildCardCount += wildCards;
        }
        /* else retain the parsing we did previously in rangeParse */
        else {
            c = stringReplace(regexp, PseudoWildcards, replacePseudo, &wildCards);
            free(regexp);
            regexp = c;
            wildCardCount += wildCards;
        }
    }

    if (wildCardCount) {
        /* compile regular expression */
        err = cMsgRegcomp(compRegexp, regexp, REG_EXTENDED);
        if (err != 0) {
            return (CMSG_ERROR);
        }
    }
    
    if (pRegexp) *pRegexp = regexp;
    if (pRange)  *pRange  = range;
    if (rCount)  *rCount  = rangeCount;
    if (wcCount) *wcCount = wildCardCount;
    
    return (CMSG_OK);
}

    
    
/**
 * This routine takes a subscription's subject or type (or any string)
 * and searches for pseudo wildcard number ranges.
 * These ranges are of the format {i>5 & 10>i & i=66}.
 * Once found, each range is parsed and results are returned in the args.
 * 
 * @param subtyp subscription's subject or type
 * @param subRegexp  pointer which gets filled in with a generated regular expression
 *                   from subtyp if rangeCount > 0
 * @param subRange   pointer which gets filled in with number range information
 *                   from subtyp if rangeCount > 0
 * @param rangeCount pointer which gets filled with the number of valid ranges found
 * 
 * @return CMSG_OK            if everything OK
 * @return CMSG_ERROR         if cannot compile regular expression
 * @return CMSG_BAD_ARGUMENT  if sub argument or subject/type is NULL
 * @return CMSG_OUT_OF_MEMORY if out of memory
 */
static int rangeParse(char *subtyp, char **subRegexp, numberRange **subRange, int *rangeCount) {

    int i, err, len, debug=0, strLen1, strLen2;
    int validRange=0, isHead=0, isTopHead=0;
    regmatch_t matches[4], matches2[5]; /* pull sub strings out of matches */
    regex_t    compiled, compiled1, compiled2;
    char *styp;
    char *buf, *buf2, *b2, *regexp;
    numberRange *range, *head, *prevHead=NULL, *topHead=NULL, *prevRange=NULL;

    if (subtyp == NULL) return (CMSG_BAD_ARGUMENT);
    isTopHead = 1;

    /* escape regexp chars except bar '|' (since that's used in ranges) */
    styp = subtyp = stringEscapeNoBar(subtyp);
    if (debug) printf("Escaped string = %s, pointer = %p\n", subtyp, subtyp);
    
    /* make buffers to construct various strings */
    len = strlen(subtyp) + 1;
    buf = (char *) malloc(len);
    if (buf == NULL) {
        free(styp);
        return(CMSG_OUT_OF_MEMORY);
    }
    
    b2 = buf2 = (char *) malloc(len);
    if (buf2 == NULL) {
        free(buf);free(styp);
        return(CMSG_OUT_OF_MEMORY);
    }
    
    /* Build a real regular expression string from the given string by
     * subsituting [0-9]+ for all properly formatted ranges {...}. */
    regexp = (char *) malloc(3*len);
    if (regexp == NULL) {
        free(buf);free(buf2);free(styp);
        return(CMSG_OUT_OF_MEMORY);
    }
    regexp[0] = '\0';

    /* compile regular expression */
    err = cMsgRegcomp(&compiled, exprRange, REG_EXTENDED);
    if (err != 0) {
        if (debug) printf("Error compiling pattern\n");
        free(buf);free(buf2);free(regexp);free(styp);
        return (CMSG_ERROR);
    }

    /* go thru string and find all matches to {...} */
    strLen1 = strlen(subtyp);
    do {
        /* look for a match in subject or type */
        err = cMsgRegexec(&compiled, subtyp, 2, matches, 0);
        if (err != 0) {
            /* No more ranges found. */
            /* There are chars left on the end which must be added to regexp. */
            if (strLen1>0) {
                strncat(regexp, subtyp, strlen(subtyp));
                if (debug) printf("regexp = %s\n", regexp);                
            }
            if (debug) printf("No match\n");
            break;
        }

        /* found a single range */
        buf[0] = 0;
        len = matches[0].rm_eo - matches[0].rm_so;
        strncat(buf, subtyp+matches[0].rm_so, len);
        if (debug) printf("Captured full range = %s\n", buf);
        /* construct regexp string with [0-9]+ substituted for range */
        strncat(regexp, subtyp, matches[0].rm_so);
        strcat(regexp, "([0-9]+)");
        if (debug) printf("regexp = %s\n", regexp);
        
        /* parse the range */
        while (1) {
            int index=0;
            buf[0] = 0;
            len = matches[1].rm_eo - matches[1].rm_so;
            strncat(buf, subtyp+matches[1].rm_so, len);
            if (debug) printf("Captured 1st sub expression in range = %s\n", buf);

            /* reset to full size buffer */
            buf2 = b2;

            /* remove all spaces from range for simpler matching */
            for (i=0; i< strlen(buf); i++) {
                if (buf[i] != ' ') {
                    buf2[index++] = buf[i];          
                }
            }
            buf2[index] = '\0';
            if (debug) printf("Captured 1st sub expression w/ no spaces = %s\n", buf2);

            /* first check the expression's full form */
            err = cMsgRegcomp(&compiled1, exprFull, REG_EXTENDED|REG_NOSUB);
            if (err != 0) {
                if (debug) printf("Error compiling pattern\n");
                cMsgNumberRangeFree(topHead);
                cMsgRegfree(&compiled);
                free(buf);free(b2);free(regexp);free(styp);
                return (CMSG_ERROR);
            }

            err = cMsgRegexec(&compiled1, buf2, 1, matches2, 0);
            /* Error */
            if (err != 0) {
                /* There is no match to the usual format, but there may be a match
                 * to {} or {i}, so check for those here. */
                if (strlen(buf2) == 0 || strcmp(buf2, "i") == 0) {
                    if (debug) printf("Matched {} or {i}\n");
                    /* valid match */
                    validRange++;
                    isHead = 1;
                    head = (numberRange *) malloc(sizeof(numberRange));
                    cMsgNumberRangeInit(head);
                    head->numbers[0] = 1;
                    head->numbers[1] = EQ;
                    head->numbers[2] = 1;
                }
                else {
                    /* try next {...} section */
                    cMsgRegfree(&compiled1);
                    if (debug) printf("Range not in proper format, ignore\n");
                    break;
                }
            }
            /* (Probably) got a valid Range, parse and store it. */
            else {
                if (debug) printf("range has good format\n"); 
                cMsgRegfree(&compiled1);

                /* go thru and pick apart each sub section (number operator number conjunction*) */
                err = cMsgRegcomp(&compiled2, exprSec, REG_EXTENDED);
                if (err != 0) {
                    if (debug) printf("Error compiling pattern\n");
                    cMsgNumberRangeFree(topHead);
                    cMsgRegfree(&compiled);
                    free(buf);free(b2);free(regexp);free(styp);
                    return (CMSG_ERROR);
                }

                isHead = 1;
                head = range = (numberRange *) malloc(sizeof(numberRange));
                if (range == NULL) {
                    cMsgNumberRangeFree(topHead);
                    cMsgRegfree(&compiled);
                    free(buf);free(b2);free(regexp);free(styp);
                    return (CMSG_OUT_OF_MEMORY);
                }
                cMsgNumberRangeInit(range);

                /* At this point we know things are in the proper format,
                 * so go thru string and find all matches.*/
                validRange++;
                strLen2 = strlen(buf2);
                do {
                    /* should never be an error here */
                    cMsgRegexec(&compiled2, buf2, 5, matches2, 0);

                    if (!isHead) {
                        range = (numberRange *) malloc(sizeof(numberRange));
                        cMsgNumberRangeInit(range);
                    }

                    /* find first number */
                    buf[0] = 0;
                    len = matches2[1].rm_eo - matches2[1].rm_so;
                    strncat(buf, buf2+matches2[1].rm_so, len);
                    if (debug) printf("Captured 1st sub expression num = %s\n", buf);
                    if (strcmp(buf, "i") == 0){
                        range->numbers[0] = -1;
                    }
                    else {
                        range->numbers[0] = atoi(buf);
                    }

                    /* find operator */
                    buf[0] = 0;
                    len = matches2[2].rm_eo - matches2[2].rm_so;
                    strncat(buf, buf2+matches2[2].rm_so, len);
                    if (debug) printf("Captured sub expression operator = %s\n", buf);
                    if (strcmp(buf, ">") == 0){
                        range->numbers[1] = GT;
                        if (debug) printf("  storing operator = '>'\n");
                    }
                    else if (strcmp(buf, "<") == 0){
                        range->numbers[1] = LT;
                        if (debug) printf("  storing operator = '<'\n");
                    }
                    else {
                        range->numbers[1] = EQ;
                        if (debug) printf("  storing operator = '='\n");
                    }

                    /* find second number */
                    buf[0] = 0;
                    len = matches2[3].rm_eo - matches2[3].rm_so;
                    strncat(buf, buf2+matches2[3].rm_so, len);
                    if (debug) printf("Captured 2nd sub expression num = %s\n", buf);
                    if (strcmp(buf, "i") == 0){
                        range->numbers[2] = -1;
                    }
                    else {
                        range->numbers[2] = atoi(buf);
                    }

                    /* Actually, there are two more improper formats to check for as in
                     * i compared to i or number compared to number (eg. i>i or 20<10). */
                    if ((range->numbers[0] == -1 && range->numbers[2] == -1) ||
                        (range->numbers[0] != -1 && range->numbers[2] != -1))  {
                        if (debug) printf("Double i or double ints\n");
                        cMsgNumberRangeFree(head);
                        validRange--;
                        goto nextRange;
                    }

                    /* find conjunction */
                    if (matches2[4].rm_so < 0) {
                        /* no conjunction, must be the end */
                        if (debug) printf("No conjunction\n");
                    } else {
                        buf[0] = 0;
                        len = matches2[4].rm_eo - matches2[4].rm_so;
                        strncat(buf, buf2+matches2[4].rm_so, len);
                        if (debug) printf("Captured sub expression conjunction = %s\n", buf);
                        if (strcmp(buf, "&") == 0){
                            range->numbers[3] = AND;
                        }
                        else {
                            range->numbers[3] = OR;
                        }
                    }
                    
                    /* linked list of results stuff */
                    if (isHead) {
                        isHead = 0;
                    }
                    else {
                        prevRange->next = range;
                    }
                    prevRange = range;
                    
                    /* move to next part of range to parse */
                    if (matches2[0].rm_eo < strLen2) {
                        buf2    += matches2[0].rm_eo;
                        strLen2 -= matches2[0].rm_eo;
                        if (debug) printf("\nEscaped string = %s\n", buf2);
                    }
                    else break;

                } while (strLen2 > 0);

                cMsgRegfree(&compiled2);
                
            } /* else if match to normal range format {...} */
            
            /* linked list of results stuff */
            if (isTopHead) {
                topHead = head;
                isTopHead = 0;
            }
            else {
                prevHead->nextHead = head;
            }
            prevHead = head;
            break;
        } /* while(1) */

        /* move to next range to parse */
        nextRange:
        if (matches[0].rm_eo < strLen1) {
            subtyp  += matches[0].rm_eo;
            strLen1 -= matches[0].rm_eo;
            if (debug) printf("\nEscaped string = %s\n", subtyp);
        }
        else break;

    } while (strLen1 > 0);
    
    /* if we found at least 1 valid range ... */
    if (validRange) {
        /* return results in pointer args */
        if (subRegexp) *subRegexp = stringEscapeBar(regexp);
        if (subRange)  *subRange  = topHead;
    }
    
    if (rangeCount) *rangeCount = validRange;
    
    /* free up memory */
    cMsgRegfree(&compiled);
    free(buf);free(b2);free(regexp);free(styp);
   
    return (CMSG_OK);
}


/**
 * This routine takes a subscription's regular expression (compiled)
 * taken from either the subject or type and sees if it matches a given string.
 * This is only called if there are wildcards in the subject/type.
 * 
 * @param subtyp string containing message's subject or type
 * @param regexp subscription's compiled regular expression to match to subtyp
 * @param topHead subscription's pointer to number-range information
 * @param rangeCount subscription's count of the number of ranges
 * @param match pointer to int gets filled with 0 if no match, else 1
 * 
 * @return CMSG_OK            if everything OK
 * @return CMSG_BAD_ARGUMENT  if sub or subtype argument is NULL
 * @return CMSG_OUT_OF_MEMORY if out of memory
 */
static int regexpMatch(char *subtyp, regex_t *compiled,
                       numberRange *topHead, int rangeCount, int *match) {
    char *buf;
    int i, err, len, debug=0, num1, num2, myNum, conj=-1, lastBool, myBool;
    regmatch_t *matches = NULL;
    numberRange *nextHead, *range;
    
    /* check args */
    if (subtyp == NULL || compiled == NULL ||
            (rangeCount > 0 && topHead == NULL) ) {
        return (CMSG_BAD_ARGUMENT);
    }
        
    /* make buffer to hold int as string */
    buf = (char *) malloc(12);
    if (buf == NULL) {
        return(CMSG_OUT_OF_MEMORY);
    }
    
    /* look for a match */
    if (debug) printf("\nNow try to match our subscription's regexp with s = %s\n", subtyp);
    if (debug) printf("  range count = %d\n", rangeCount);
    matches = (regmatch_t *) malloc((rangeCount+1)*sizeof(regmatch_t));
    if (matches == NULL)
        return(CMSG_OUT_OF_MEMORY);

    err = cMsgRegexec(compiled, subtyp, rangeCount+1, matches, 0);
    if (err != 0) {
        if (debug) printf("  No match\n");
        free(buf);
        if (match != NULL) *match = 0;
        return (CMSG_OK);
    }
    
    /* if there are ranges there are more matches to examine */
    if (rangeCount) {
        range = topHead;        
        nextHead = topHead->nextHead;        

        for (i=1; i<=rangeCount; i++) {
            /* find number */
            buf[0] = 0;
            len = matches[i].rm_eo - matches[i].rm_so;
            strncat(buf, subtyp+matches[i].rm_so, len);
            if (debug) printf("Captured number[%d] = %s\n", i, buf);
            myNum = atoi(buf);
            lastBool = 1;
            while (range != NULL) {
                if (range->numbers[0] == -1) num1 = myNum;
                else num1 = range->numbers[0];

                if (range->numbers[2] == -1) num2 = myNum;
                else num2 = range->numbers[2];

                if (range->numbers[1] == LT) {
                    myBool = num1 < num2 ? 1 : 0;
                    if (debug) printf("    %d < %d ?\n", num1, num2);
                }
                else if (range->numbers[1] == GT) {
                    myBool = num1 > num2 ? 1 : 0;
                    if (debug) printf("    %d > %d ?\n", num1, num2);
                }
                else {
                    myBool = num1 == num2 ? 1 : 0;                   
                    if (debug) printf("    %d == %d ?\n", num1, num2);
                }

                if (conj > 0) {
                    if (conj == AND) {
                        myBool = myBool && lastBool;
                        if (debug) printf("    conj = &&\n");
                    }
                    else {
                        myBool = myBool || lastBool;
                        if (debug) printf("    conj = ||\n");
                    }
                }
                /*
                else {
                    if (debug) printf("    no conj\n");
                }
                */
                conj = range->numbers[3];

                lastBool = myBool;               
                if (debug) printf("    After lastBool = %d\n", lastBool);

                range = range->next;
            }
            
            if (!lastBool) {
                if (debug) printf("Return FALSE\n");
                free(buf);
                if (match != NULL) *match = 0;
                return (CMSG_OK);
            }
            
            range = nextHead;
            if (range) nextHead = range->nextHead;
        }
    }
   
    free(buf);
    if (match) *match = 1;
    return (CMSG_OK);
}
