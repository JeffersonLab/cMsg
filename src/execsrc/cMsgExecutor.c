/*----------------------------------------------------------------------------*
 *  Copyright (c) 2010        Southeastern Universities Research Association, *
 *                            Jefferson Science Associates                    *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C.Timmer, 10-Nov-2010, Jefferson Lab                                    *
 *                                                                            *
 *    Authors: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, #10           *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-6248             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/
 
/**
 * @file
 * This file defines the Executor which is software designed to take commands
 * from a Commander. In short, the Commander tells an Executor which program
 * to run on its host in a shell. The Executor runs it and reports the results
 * back to the Commander. This code is designed to run on VXWORKS only.<p>
 *
 * The following is a boot script I used to run an executor on vxworks 6.0:<p>
 *
 * <pre><code>
 * hostAdd "alula", "129.57.29.90"
 * hostAdd "alula.jlab.org", "129.57.29.90"
 * cd "/group/da/ct/cMsg-3.3/vxworks-ppc"
 * ld &lt; lib/libcmsgRegex.o
 * ld &lt; lib/libcmsg.o
 * ld &lt; bin/cMsgExecutor.o
 * taskSpawn "executor", 51, "VX_FP_TASK", 20000, executorMain, "cmsg://alula/cMsg/ns", "heyho"
 * taskDelay(5*60)
 * </code></pre>
 */

#ifdef VXWORKS

#include <vxWorks.h>
#include <taskLib.h>
#include <symLib.h>
#include <symbol.h>
#include <sysSymTbl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <pthread.h>

#include "hash.h"
#include "cMsgPrivate.h"
#include "cMsgNetwork.h"
#include "polarssl_aes.h"

char *strndup(const char *s, size_t n);

/** This structure stores information about a received command. */
typedef struct commandInfo_t {
    char *className;
    char *command;
    char *commander;
    int   commandId;
    int   monitor;
    int   wait;
    int   id;
    int   shellId;
    int   killed;
    int   stopped;
    void *argsMessage;
} commandInfo;


/**
 * Initialize commandInfo structure.
 * @param info pointer to commandInfo structure.
 */
static void initCommandInfo(commandInfo *info) {
    info->className   = NULL;
    info->command     = NULL;
    info->commander   = NULL;
    info->argsMessage = NULL;
    info->commandId   = 0;
    info->monitor     = 0;
    info->wait        = 0;
    info->id          = 0;
    info->shellId     = 0;
    info->killed      = 0;
    info->stopped     = 0;
}


/**
 * Structure for more than 1 argument to be passed to a thread or callback.
 * This allows us to avoid using static variables by passing everything
 * around in an arg, thereby making the code reentrant.
 */
typedef struct passedArg_t {
    void *arg1;
    void *arg2;
    void *arg3;
    void *arg4;
    void *arg5;
} passedArg;



/** cMsg message subject or type used in internal Commander/Executor communication. */
static const char* allSubjectType = ".all";

/** cMsg message subject or type used in internal Commander/Executor communication. */
static const char* remoteExecSubjectType = "cMsgRemoteExec";

/** Pthread mutex serializing calls to getUniqueId(). */
static pthread_mutex_t idMutex = PTHREAD_MUTEX_INITIALIZER;

/** Pthread mutex serializing calls to hash table routines. */
static pthread_mutex_t hashMutex = PTHREAD_MUTEX_INITIALIZER;

/** Unique id to assign incoming requests. */
static int uniqueId = 0;

/** Unique id to assign psuedo terminals. */
static int ptyId = 0;


/* prototypes of static functions */
static void  processMapPut(hashTable *pTable, int id, void *info);
static void  processMapRemove(hashTable *pTable, int id);
static int   getUniqueId();
static int   getPtyId();
static int   sendStatusTo(void *domainId, void *msg, const char *subject);
static void *createStatusMessage(const char *name, const char *host);
static int   decryptString(const char *string, int len, char **result);
static void  callback(void *msg, void *arg);
static void *processThread(void *arg);
static void  stop(int id, hashTable *pTable);
static void  stopAll(int kill, hashTable *pTable);
static time_t timeDiff(struct timespec *t2, struct timespec *t1);
static int   strCharCount(const char *str1, const char *str2);
static int   strStringCount(const char *str1, const char *str2);
static char *upBufferSize(char *buffer, size_t bufLen, size_t size);
static char *removeCmdEcho(char *buffer, char *cmd);
static void  removeLastValueEquals(char *buf);
static int   sendResponseMsg(void *domainId, void *msg, const char *error,
                             int terminated, int immediate);

void executorMain(char *udl, char *password, char *name);


/** AES encryption key. */
static unsigned char AESkey[16] = {-121, -59, -26,  12,
                                    -51, -29, -26,  86,
                                    110,  25, -23, -27,
                                    112, -80,  77, 102 };


/**
 * This routine puts a key-value pair into a hash table.
 * 
 * @param pTable pointer to hash table
 * @param id key (first converted to string)
 * @param info value
 */
static void processMapPut(hashTable *pTable, int id, void *info) {
    int status;
    char key[10];
    
    /* change id into string so it can be hash table key */
    sprintf(key, "%d", id);

    status = pthread_mutex_lock(&hashMutex);
    if (status != 0) {
        cmsg_err_abort(status, "Failed processMapPut mutex lock");
    }
    
    hashInsert(pTable, key, (void *)info, NULL);
    
    status = pthread_mutex_unlock(&hashMutex);
    if (status != 0) {
        cmsg_err_abort(status, "Failed processMapPut mutex unlock");
    }
}


/**
 * This routine removes an entry from a hash table.
 *
 * @param pTable pointer to hash table
 * @param id key of entry to remove
 */
static void processMapRemove(hashTable *pTable, int id) {
    int status;
    char key[10];
    
    /* change id into string so it can be hash table key */
    sprintf(key, "%d", id);

    status = pthread_mutex_lock(&hashMutex);
    if (status != 0) {
        cmsg_err_abort(status, "Failed processMapPut mutex lock");
    }

    if (hashRemove(pTable, key, NULL)) {
/*printf("Removed key = %s from hash\n", key);*/
    }
    else {
/*printf("Failed removing key = %s from hash\n", key);*/
    }
/*printf("New hash table size = %d\n", hashSize(pTable));*/

    status = pthread_mutex_unlock(&hashMutex);
    if (status != 0) {
        cmsg_err_abort(status, "Failed processMapPut mutex unlock");
    }
}


/**
 * This routine generates and returns the next unique request id number.
 *
 * @return unique id number
 */
static int getUniqueId() {
    int id, status;
    
    status = pthread_mutex_lock(&idMutex);
    if (status != 0) {
        cmsg_err_abort(status, "Failed getUniqueId mutex lock");
    }
    
    id = uniqueId = (uniqueId + 1)%INT_MAX;
    
    status = pthread_mutex_unlock(&idMutex);
    if (status != 0) {
        cmsg_err_abort(status, "Failed getUniqueId mutex unlock");
    }
    
    return id;
}

/**
 * This routine generates and returns the next unique pty id number.
 *
 * @return unique pty id number
 */
static int getPtyId() {
    int id, status;
    
    status = pthread_mutex_lock(&idMutex);
    if (status != 0) {
        cmsg_err_abort(status, "Failed getPtyId mutex lock");
    }
    
    id = ptyId = (ptyId + 1)%INT_MAX;

    status = pthread_mutex_unlock(&idMutex);
    if (status != 0) {
        cmsg_err_abort(status, "Failed getPtyId mutex unlock");
    }
    
    return id;
}


/**
 * This routine sends a status cMsg message to the given subject.
 *
 * @param domainId id specifying a particular cMsg connection
 * @param msg cMsg message to send
 * @param subject subject of message to send
 * @return status of cMsgSend command
 */
static int sendStatusTo(void *domainId, void *msg, const char *subject) {
    /*printf("sendStatusTo: %s\n", subject);*/
    int err = cMsgSetSubject(msg, subject);
    if (err != CMSG_OK) return err;
    return cMsgSend(domainId, msg);
}


/**
 * This is the main function for running Executor (used this as "main"
 * for vxworks). This function tries to maintain a connection to the
 * specified cMsg server.
 * 
 * @param _udl      udl for connecting to cMsg server
 * @param _password password Commander needs to provide for
 *                  this Executor to execute any commands.
 * @param _name     name for cMsg connection
 */
void executorMain(char *_udl, char *_password, char *_name) {

    char *udl, *password, *name;
    char *myDescription = "cmsg C executor";
    char  host[CMSG_MAXHOSTNAMELEN];
    int   err, status, quit=0, connected=0, previouslyConnected=0;
    void *domainId, *unsubHandle1, *unsubHandle2, *statusMsg;
    cMsgSubscribeConfig *config;
    passedArg args;
    struct timespec wait = {1, 0}; /* 1 sec */
    /* Hash table with key = unique id (as string) and value = commandInfo. */
    hashTable idTable;

    /* Init pseudo terminal driver. */
    if (ptyDrv () == ERROR) {
        printf("executorMain: Unable to initialize pty driver\n");
        exit(-1);
    }
    
    /* Process args, udl is necessary. */
    if (_udl == NULL) {
        printf("executorMain: udl must not be NULL\n");
        exit(-1);
    }

    /* Limit length of password to 16 chars. */
    if (_password != NULL) {
        if (strlen(_password) > 16) {
            printf("executorMain: password must not be more than 16 characters\n");
            exit(-1);
        }
    }

    /* Create subscription configuration. */
    config = cMsgSubscribeConfigCreate();
    if (config == NULL) {
        printf("executorMain: cannot allocate memory\n");
        exit(-1);
    }
   
    /* Find the name of the host we're running on. */
    err = cMsgLocalHost(host, CMSG_MAXHOSTNAMELEN);
    if (err != CMSG_OK) {
        printf("executorMain: cannot get host name\n");
        exit(-1);
    }

    /* If Executor is not given a name, use the host as its name. */
    if (_name == NULL) {
        _name = host;
    }

    /* Create a status message to send to Commanders telling them about this Executor. */
    statusMsg = createStatusMessage(_name, host);
    if (statusMsg == NULL) {
        printf("executorMain: cannot allocate memory\n");
        exit(-1);
    }
      
    /*
    * In vxworks 6.0, at least when running a script, the arguments given in taskSpawn are
    * NOT pointing to permanent memory once that script has ended and must be copied.
    */
    udl      = strdup(_udl);
    name     = strdup(_name);
    password = strdup(_password);

    hashInit(&idTable, 64);

printf("Running Executor %s\n", name);

    /* Make a connection to a cMsg server or keep trying to connect. */
    while (1) {
        
        /* 1 sec delay */
        nanosleep(&wait, NULL);

        /* Mutex needed here so we can see the flag value set by another thread. */
        status = pthread_mutex_lock(&idMutex);
        if (status != 0) {
            cmsg_err_abort(status, "Failed executorMain mutex lock");
        }
    
        if (quit) {
            pthread_mutex_unlock(&idMutex);
            break;
        }
    
        status = pthread_mutex_unlock(&idMutex);
        if (status != 0) {
            cmsg_err_abort(status, "Failed executorMain mutex unlock");
        }

        /* Are we (still) connected to the cMsg server? */
        err = cMsgGetConnectState(domainId, &connected);
        if (err != CMSG_OK) {
/*printf("executorMain: NOT connected to cMsg server\n");*/
            connected = 0;
        }

        if (!connected) {

            if (previouslyConnected) {
                cMsgDisconnect(&domainId);
                previouslyConnected = 0;
            }
            
            /* connect */
            err = cMsgConnect(udl, name, myDescription, &domainId);
            if (err != CMSG_OK) {
                continue;
            }

            connected = previouslyConnected = 1;

            cMsgReceiveStart(domainId);

            /* pass in user arg to subscriptions */
            args.arg1 = (void *)password;
            args.arg2 = (void *)&domainId;
            args.arg3 = statusMsg;
            args.arg4 = &idTable;
            args.arg5 = &quit;

            /* add subscriptions */
            err = cMsgSubscribe(domainId, remoteExecSubjectType, name, callback,
                                (void *)&args, config, &unsubHandle1);
            if (err != CMSG_OK) {
/*printf("executorMain: %s\n",cMsgPerror(err));*/
                continue;
            }

            err = cMsgSubscribe(domainId, remoteExecSubjectType, allSubjectType,
                                callback, (void *)&args, config, &unsubHandle2);
            if (err != CMSG_OK) {
/*printf("executorMain: %s\n",cMsgPerror(err));*/
                continue;
            }

            /* Send out message telling all commanders that there is a new executor running. */
            err = sendStatusTo(domainId, statusMsg, allSubjectType);
            if (err != CMSG_OK) {
/*printf("executorMain: %s\n",cMsgPerror(err));*/
                continue;
            }
        }
    }

printf("Executor quitting\n");

    cMsgDisconnect(&domainId);
    cMsgFreeMessage(&statusMsg);
    hashDestroy(&idTable, NULL, NULL);
    free(udl);
    if (name != NULL) free (name);
    if (password != NULL) free(password);
}


/**
 * Create a status message sent out to all commanders as soon
 * as we connect to the cMsg server and to all commanders who
 * specifically ask for it. Returned msg must be freed by caller.
 *
 * @param name name of this Executor.
 * @param host host this Executor is running on.
 * @return cMsg status message or NULL if memory cannot be allocated
 */
static void *createStatusMessage(const char *name, const char *host) {

    int err;
    char *os, *machine, *processor, *release;
    void *statusMsg = cMsgCreateMessage();

    if (statusMsg == NULL) {
        return NULL;
    }
    
    cMsgSetHistoryLengthMax(statusMsg, 0);

    /* The subject of this msg gets changed depending on who it's sent to. */
    cMsgSetSubject(statusMsg, allSubjectType);
    cMsgSetType(statusMsg, remoteExecSubjectType);

    err = cMsgAddString(statusMsg, "returnType", "reporting");
    if (err != CMSG_OK) {
        /* out-of-memory = only possible error at this point */
        return NULL;
    }
    
    err = cMsgAddString(statusMsg, "name", name);
    if (err != CMSG_OK) {
        return NULL;
    }
    
    err = cMsgAddString(statusMsg, "host", host);
    if (err != CMSG_OK) {
        return NULL;
    }
    
    os        = "vxworks";
    release   = "6.0";
    machine   = "unknown";
    processor = "unknown";

    err = cMsgAddString(statusMsg, "os", os);
    if (err != CMSG_OK) {
        return NULL;
    }
    
    err = cMsgAddString(statusMsg, "machine", machine);
    if (err != CMSG_OK) {
        return NULL;
    }
    
    err = cMsgAddString(statusMsg, "processor", processor);
    if (err != CMSG_OK) {
        return NULL;
    }

    err = cMsgAddString(statusMsg, "release", release);
    if (err != CMSG_OK) {
        return NULL;
    }
    
    return statusMsg;
}


/**
 * Decrypt the given encrypted string into a recognizable string.
 * This routine allocates memory for the returned string
 * which must be freed by the caller.
 *
 * @param string input string
 * @param len length (number of characters) in non-encrypted string
 * @param result pointer to string which will point to the decrypted string
 * @return CMSG_OK or CMSG_OUT_OF_MEMORY
 */
static int decryptString(const char *string, int len, char **result) {
    char *pString, *origString, *pBytes, *bytes;
    aes_context ctx;
    unsigned int bytesLen;
    int numBytes, lenSoFar=0;
    
    /* Number of bytes in decoded B64 string.
     * Java's ecb encoded string comes in chunks of 16 bytes
     * so bytesLen should be in multiples of 16.*/
    bytesLen = cMsg_b64_decode_len(string, strlen(string));

    /* be extra careful here (though I think we don't really need to do this) */
    if (bytesLen % 16 != 0) {
        bytesLen = ((bytesLen - 1)/16)*16 + 16;
    }

    pBytes = bytes = (char *) calloc(1, bytesLen);
    if (bytes == NULL) {
        return CMSG_OUT_OF_MEMORY;
    }
    
    /* string is in B64 form, decode to byte array */
    numBytes = cMsg_b64_decode(string, strlen(string), bytes);

    /* Initialize context structure. */
    aes_setkey_dec(&ctx, AESkey, 128);

    /* create space for the decrypted string */
    pString = origString = (char *) calloc(1, len+1);
    if (origString == NULL) {
        free(bytes);
        return CMSG_OUT_OF_MEMORY;
    }

    while (lenSoFar < len) {
        /* Decrypt bytes into original string 16 bytes at a time. */
        aes_crypt_ecb(&ctx, AES_DECRYPT, pBytes, pString);

        /* move on to next 16 bytes */
        pBytes   += 16;
        pString  += 16;
        lenSoFar += 16;
    }

    free(bytes);
    
    if (result != NULL) {
        /*
         * I don't know what goop this C lib puts at the end of
         * the decrypted string, so make sure there's a null at the end.
         */
        origString[len] = '\0';
        *result = origString;
    }
    
    return CMSG_OK;
}


/**
 * This routine defines the callback to be run when a message
 * containing a valid command arrives.
 *
 * @param msg cMsg message being passed to this callback
 * @param arg user argument being passed to this callback
 */
static void callback(void *msg, void *arg) {

    int err, status, payloadCount, *pQuit, debug=0;
    int32_t intVal;
    char *passwd = NULL;
    const char *val, *commandType;
    pthread_t tid;
    
    /*
     * The domain id may change if a new cMsg connection is made.
     * Thus, we passed the pointer to the domain Id so it can be
     * refreshed each time this callback is run.
     */
    void *domainId;
    char *password;
    void *statusMsg;
    hashTable *pTable;
    passedArg *args2, *args = (passedArg *)arg;

    domainId  = (void *) *((void **)args->arg2);
    password  = (char *)args->arg1;
    statusMsg = args->arg3;
    pTable    = (hashTable *)args->arg4;
    pQuit     = (int *)args->arg5;

    /* There must be a payload. */
    cMsgHasPayload(msg, &payloadCount);
    
    if (payloadCount < 1) {
        if (debug) printf("Reject message, bad format - no payload items");
        cMsgFreeMessage(&msg);
        return;
    }
    
    err = cMsgGetString(msg, "p", &val);
    if (err == CMSG_OK) {
        int32_t pswdLen;
        err = cMsgGetInt32(msg, "pl", &pswdLen);
        if (err != CMSG_OK) {
            if (debug) printf("Reject message, no password length");
            cMsgFreeMessage(&msg);
            return;
        }

        /* decrypt password here (val cannot be NULL) */
        err = decryptString(val, pswdLen, &passwd);
        if (err != CMSG_OK) {
            if (debug) printf("Cannot allocate memory");
            cMsgFreeMessage(&msg);
            return;
        }
        if (debug) printf("Decrypted password -> -----%s-----\n", passwd);
    }

    /* check password if required */
    if (password != NULL && strcmp(password, passwd) != 0) {
        if (debug) printf("Wrong password");
        cMsgFreeMessage(&msg);
        if (passwd != NULL) free(passwd);
        return;
    }

    if (passwd != NULL) free(passwd);

    /* What command are we given? */
    err = cMsgGetString(msg, "commandType", &commandType);
    if (err != CMSG_OK) {
        if (debug) printf("Reject message, no command type\n");
        cMsgFreeMessage(&msg);
        return;
    }
        
    if (debug) printf("commandtype = %s\n", commandType);

    if (strcmp(commandType, "start_process") == 0) {
        /* Store incoming data here */
        int32_t cmdLen;
        int monitor, wait, isGetRequest;
        passedArg *arg;
        void *responseMsg;
        commandInfo *info;
        char *origCmd = NULL;

        /* Is the msg from a sendAndGet? */
        isGetRequest = 0;
        cMsgGetGetRequest(msg, &isGetRequest);
        if (!isGetRequest) {
            if (debug) printf("Reject message, start_process cmd must be sendAndGet msg");
            cMsgFreeMessage(&msg);
            return;
        }

        info = (commandInfo *) malloc(sizeof(commandInfo));
        if (info == NULL) {
            if (debug) printf("Cannot allocate memory");
            cMsgFreeMessage(&msg);
            return;
        }
        initCommandInfo(info);

        err = cMsgGetString(msg, "command", &val);
        if (err != CMSG_OK) {
            if (debug) printf("Reject message, no command");
            free(info);
            cMsgFreeMessage(&msg);
            return;
        }
        
        err = cMsgGetInt32(msg, "commandLen", &cmdLen);
        if (err != CMSG_OK) {
            if (debug) printf("Reject message, no command length");
            free(info);
            cMsgFreeMessage(&msg);
            return;
        }

        /* decrypt command here (val cannot be NULL) */
        err = decryptString(val, cmdLen, &origCmd);
        if (err != CMSG_OK) {
            if (debug) printf("Cannot allocate memory");
            free(info);
            cMsgFreeMessage(&msg);
            return;
        }
if (debug) printf("Decrypted cmd -> -----%s-----\n", origCmd);

        info->command = origCmd;

        monitor = 0;
        err = cMsgGetInt32(msg, "monitor", &intVal);
        if (err == CMSG_OK) {
            monitor = intVal;
        }
        info->monitor = monitor;

        wait = 0;
        err = cMsgGetInt32(msg, "wait", &intVal);
        if (err == CMSG_OK) {
            wait = intVal;
        }
        info->wait = wait;

        err = cMsgGetString(msg, "commander", &val);
        if (err != CMSG_OK) {
            if (debug) printf("Reject message, no commander");
            free(info->command);
            free(info);
            cMsgFreeMessage(&msg);
            return;
        }
        info->commander = strdup(val);
        if (info->commander == NULL) {
            free(info->command);
            free(info);
            cMsgFreeMessage(&msg);
            return;
        }

        err = cMsgGetInt32(msg, "id", &intVal);
        if (err != CMSG_OK) {
            if (debug) printf("Reject message, no commander id");
            free(info->commander);
            free(info->command);
            free(info);
            cMsgFreeMessage(&msg);
            return;
        }
        info->commandId = intVal;

        /* Return must be placed in sendAndGet response msg. */
        responseMsg = cMsgCreateResponseMessage(msg);
        if (responseMsg == NULL) {
            if (debug) printf("Cannot allocate memory 1");
            free(info->commander);
            free(info->command);
            free(info);
            cMsgFreeMessage(&msg);
            return;
        }

        /* Create arg to new thread. */
        args2 = (passedArg *) malloc(sizeof(passedArg));
        if (args2 == NULL) {
            if (debug) printf("Cannot allocate memory 2");
            free(info->commander);
            free(info->command);
            free(info);
            cMsgFreeMessage(&msg);
            cMsgFreeMessage(&responseMsg);
            return;
        }
        args2->arg1 = (void *)info;
        args2->arg2 = responseMsg;
        args2->arg3 = domainId;
        args2->arg4 = args->arg4; /* pass along hash table pointer */

        /* Start up new thread. */
        status = pthread_create(&tid, NULL, processThread, (void *)args2);
        if (status != 0) {
            cmsg_err_abort(status, "Cannot create process thread");
        }
    }
    
    else if (strcmp(commandType, "start_thread") == 0) {
        void *responseMsg = cMsgCreateResponseMessage(msg);
        if (responseMsg != NULL) {
            sendResponseMsg(domainId, responseMsg, "\"start_thread\" cmd is not supported on vxworks", 1, 1);
        }
        cMsgFreeMessage(&responseMsg);
    }
    
    else if (strcmp(commandType, "stop_all") == 0) {
        if (debug) printf("Got STOPALL msg\n");
        stopAll(0, pTable);
    }
    
    else if (strcmp(commandType, "stop") == 0) {
        if (debug) printf("Got STOP msg\n");
        err = cMsgGetInt32(msg, "id", &intVal);
        if (err != CMSG_OK) {
            if (debug) printf("Rejectstop  message, no id");
            cMsgFreeMessage(&msg);
            return;
        }
        stop(intVal, pTable);
    }
    
    else if (strcmp(commandType, "die") == 0) {
        int killProcesses = 0;
        if (debug) printf("Got DIE msg\n");
        err = cMsgGetInt32(msg, "killProcesses", &intVal);
        if (err == CMSG_OK) {
            killProcesses = intVal;
        }

        if (killProcesses) stopAll(1, pTable);

        /*
         * Mutex used here so main thread can see new value of "quit".
         * Quitting is handled in the main thread.
         */
        status = pthread_mutex_lock(&idMutex);
        if (status != 0) {
            cmsg_err_abort(status, "Failed callback mutex lock");
        }

        *pQuit = 1;

        status = pthread_mutex_unlock(&idMutex);
        if (status != 0) {
            cmsg_err_abort(status, "Failed callback mutex unlock");
        }
    }
    
    else if (strcmp(commandType, "identify") == 0) {
        err = cMsgGetString(msg, "commander", &val);
        if (err != CMSG_OK) {
            if (debug) printf("Reject message, no commander");
            cMsgFreeMessage(&msg);
            return;
        }

        sendStatusTo(domainId, statusMsg, val);
    }
    
    else {
        if (debug) printf("Reject message, invalid command");
    }
    
    cMsgFreeMessage(&msg);
}


/**
 * Routine to stop the given command.
 * @param id id of command to stop
 * @param pTable pointer to hash table struct
 */
static void stop(int id, hashTable *pTable) {
    int status;
    char key[10];
    commandInfo *info;
  
    /* change id into string so it can be hash table key */
    sprintf(key, "%d", id);

    status = pthread_mutex_lock(&hashMutex);
    if (status != 0) {
        cmsg_err_abort(status, "Failed stop mutex lock");
    }

    if (!hashRemove(pTable, key, (void **)&info)) {
        status = pthread_mutex_unlock(&hashMutex);
        if (status != 0) {
            cmsg_err_abort(status, "Failed stop mutex unlock");
        }
        return;
    }

    status = pthread_mutex_unlock(&hashMutex);
    if (status != 0) {
        cmsg_err_abort(status, "Failed stop mutex unlock");
    }
    
    /*
     * Since one thread is changing the flag and another is reading
     * it, the correct value of the flag can only be seen if
     * written & read with held mutex.
     */
    status = pthread_mutex_lock(&idMutex);
    if (status != 0) {
        cmsg_err_abort(status, "Failed stop mutex lock");
    }
    
    info->stopped = 1;

    status = pthread_mutex_unlock(&idMutex);
    if (status != 0) {
        cmsg_err_abort(status, "Failed stop mutex unlock");
    }

    shellTerminate(info->shellId);
}


/**
 * Routine to stop all commands.
 * @param kill are we killing the executor (<code>true</code>),
 *             or just stopping all processes (<code>false</code>)
 * @param pTable pointer to hash table struct
 */
static void stopAll(int kill, hashTable *pTable) {

    int i, size, status;
    hashNode *entries;
    commandInfo *info;
    
    status = pthread_mutex_lock(&hashMutex);
    if (status != 0) {
        cmsg_err_abort(status, "Failed stopAll mutex lock");
    }

    if (hashClear(pTable, &entries, &size)) {
        /*
         * Since one thread is changing the flag and another is reading
         * it, the correct value of the flag can only be seen if
         * written & read with held mutex.
         */
        status = pthread_mutex_lock(&idMutex);
        if (status != 0) {
            cmsg_err_abort(status, "Failed stop mutex lock");
        }
    
        for (i=0; i < size; i++) {
            /* key must be freed when calling hashClear and 2nd arg != NULL */
            free((void *)entries[i].key);
            info = (commandInfo *)entries[i].data;
      
            if (kill) {
                info->killed = 1;
/*printf("stopAll(): kill id = %s\n", entries[i].key);*/
            }
            else {
                info->stopped = 1;
/*printf("stopAll(): stop id = %s\n", entries[i].key);*/
            }
            shellTerminate(info->shellId);
        }
        
        status = pthread_mutex_unlock(&idMutex);
        if (status != 0) {
            cmsg_err_abort(status, "Failed stop mutex unlock");
        }
    }

    status = pthread_mutex_unlock(&hashMutex);
    if (status != 0) {
        cmsg_err_abort(status, "Failed stopAll mutex unlock");
    }

    free(entries);
}


/**
 * This routine returns the time difference between 2 times
 * in timespec format in units of milliseconds.
 *
 * @param t2 later time
 * @param t1 earlier time
 * @return time difference in milliseconds
 */

static time_t timeDiff(struct timespec *t2, struct timespec *t1) {
    return (t2->tv_sec - t1->tv_sec)*1000 + (t2->tv_nsec - t1->tv_nsec)/1000000;
}


/**
 * This routine counts the number of times that characters in str2 occur in str1.
 *
 * @param str1 NULL-terminated string in which search.
 * @param str2 NULL-terminated string containing characters to look for.
 * @return number of times that characters in str2 occur in str1.
 */
static int strCharCount(const char *str1, const char *str2) {
    int count=0;
    if (str1 == NULL || str2 == NULL) return 0;
    while (*str1 != '\0') {
        str1 = strpbrk(str1, str2);
        if (str1 == NULL) break;
        str1++;
        count++;
    }
    return count;
}

/**
 * This routine counts the number of times that the whole string str2 occurs in str1.
 *
 * @param str1 NULL-terminated string in which search.
 * @param str2 NULL-terminated string to look for.
 * @return number of times that the whole string str2 occurs in str1.
 */
static int strStringCount(const char *str1, const char *str2) {
    int str2Len,count=0;
    if (str1 == NULL || str2 == NULL) return 0;
    str2Len = strlen(str2);
    while (*str1 != '\0') {
        str1 = strstr(str1, str2);
        if (str1 == NULL) break;
        str1 += str2Len;
        count++;
    }
    return count;
}

/**
 * This routine increases a buffer's size by copying it into a new, larger buffer
 * which was initialized to all zeros.
 * Original buffer is freed. New buffer will need to eventually be freed.
 *
 * @param buffer buffer to increase.
 * @param bufLen size of data in buffer to copy.
 * @param size   size of new buffer in bytes.
 * @return pointer to new buffer or NULL if no memory can be allocated.
 */
static char *upBufferSize(char *buffer, size_t bufLen, size_t size) {
    char *newBuf = (char *)calloc(1, size);
    if (newBuf == NULL) {
        return NULL;
    }
    memcpy(newBuf, buffer, bufLen);
    free(buffer);
    return newBuf;
}

/**
 * This routine takes the output of a shell command and removes the initial
 * echo of the given command by returning the pointer to the character
 * following the echo.
 * The command (with newline) is echoed twice and is also preceded by a new line.
 *
 * @param buffer buffer to examine.
 * @param cmd command whose echo we want to skip (WITHOUT newline at the end).
 * @return pointer to place in buffer after echo; or NULL if no echo or NULL arg(s).
 */
static char *removeCmdEcho(char *buffer, char *cmd) {
    int   cmdLen;
    char *pChar;

    if (buffer == NULL || cmd == NULL) return NULL;

    cmdLen = strlen(cmd);

    /******************************/
    /* skip 1st command echo line */
    /******************************/
    pChar = strstr(buffer, cmd);
    if (pChar == NULL) {
        return NULL;
    }
    pChar += cmdLen;
    
    /* Now that we're past the 1st cmd, skip its whole line */
    pChar = strchr(pChar, '\n');
    if (pChar == NULL) {
        return NULL;
    }
    pChar += 1;
   
    /******************************/
    /* skip 2nd command echo line */
    /******************************/
    pChar = strstr(pChar, cmd);
    if (pChar == NULL) {
        return NULL;
    }
    pChar += cmdLen;

    /* Now that we're past the 2nd cmd, skip its whole line */
    pChar = strchr(pChar, '\n');
    if (pChar == NULL) {
        return NULL;
    }
    pChar += 1;

    return pChar;
}


/**
 * This routine takes the output of a shell command
 * and removes the last "value = " and everything after.
 *
 * @param buf buffer to examine.
 */
static void removeLastValueEquals(char *buf) {
    
    char *pLastVal=NULL, *valEquals = "value = ";
    int   valEqualsLen = strlen(valEquals);

    if (buf == NULL) return;

    while (*buf != '\0') {
        buf = strstr(buf, valEquals);
        if (buf == NULL) break;
        pLastVal = buf;
        buf += valEqualsLen;
    }

    /* If there's no "value = " in the buffer, just return. */
    if (pLastVal == NULL) {
        return;
    }

    /* Cut off the last "value = " and everything after */
    *pLastVal = '\0';
}


/**
 * This routine sends a response message to the
 * Commander currently sending us commands.
 * 
 * @param domainId cMsg connection id
 * @param msg cMsg message to send
 * @param error error string to send
 * @param terminated <code>true</code> if command terminated
 * @param immediate  <code>true</code> if command generated immediate error
 * @return status of cmsg error if any, else CMSG_OK
 */
static int sendResponseMsg(void *domainId, void *msg, const char *error,
                           int terminated, int immediate) {
    int err;
                           
    if (terminated) {
        err = cMsgAddInt32(msg, "terminated", 1);
        if (err != CMSG_OK) {
            /* only possible error at this point is out-of-memory */
            return err;
        }
    }
    
    if (immediate) {
        err = cMsgAddInt32(msg, "immediateError", 1);
        if (err != CMSG_OK) {
            return err;
        }
    }

    if (error != NULL) {
        err = cMsgAddString(msg, "error", error);
        if (err != CMSG_OK) {
            return err;
        }
    }
    
    return cMsgSend(domainId, msg);
}


/**
 * Thread to run command in. Noticed that whenever this thread is run,
 * it leaves 128 bytes of allocated memory lost. It appears to be a
 * vxworks issue since as a test I quit this thread only after the first
 * 2 statements, sending an error msg back, and freeing all memory.
 * If I never run this thread, send error msg back and free all mem,
 * no memory leak is found.
 * 
 * @param arg pointer to structure holding arguments.
 */
static void *processThread(void *arg) {

    passedArg *args    = (passedArg *) arg;
    commandInfo *info  = (commandInfo *)args->arg1;
    void *responseMsg  = args->arg2;
    void *domainId     = args->arg3;
    hashTable *idTable = (hashTable *)args->arg4;

    void *imDoneMsg;
    char **commands, *pCmdCpy;
    const char *val;
    struct timespec wait = {0, 100000000}; /* 0.1 sec */

    int i, id=0, rc, err, status, delayTicks, inHash=0, sentNotWaitingMsg=0;
    int fdSlaveS, fdMasterS, fdSlaveE, fdMasterE, shellId;
    int timeCount=0, cmdCount=0, valueCount=0, bytesUnread=0, bytesRead=0, totalBytesRead=0;
    size_t maxReadSize, bufSize=3300;
    char  ptySys[19], ptySysS[19], ptySysM[19];
    char  ptyErr[19], ptyErrS[19], ptyErrM[19];
    char *shellTaskName, *errorStr, *inBuf=NULL, *pBuf;

    free(arg);

    /* release system resources when thread finishes */
    pthread_detach(pthread_self());
    
    /*
     * Create 2 pseudo terminal devices with unique names (err<id>. & sys<id>.),
     * one for regular IO and the other for error output.
     */
    i = getPtyId();
    memset(ptyErr, 0, 19);
    memset(ptySys, 0, 19);
    sprintf(ptyErr, "err%d.", i);
    sprintf(ptySys, "sys%d.", i);

    if (ptyDevCreate (ptySys, 4096, 4096) == ERROR) {
        sendResponseMsg(domainId, responseMsg, "Unable to create pty device", 1, 1);
        cMsgFreeMessage(&responseMsg);
        free(info->commander); free(info->command); free(info);
        return;
    }
    
    if (ptyDevCreate (ptyErr, 4096, 4096) == ERROR) {
        sendResponseMsg(domainId, responseMsg, "Unable to create pty device", 1, 1);
        cMsgFreeMessage(&responseMsg);
        ptyDevRemove(ptySys);
        free(info->commander); free(info->command); free(info);
        return;
    }

    /* Names of master & slave parts of pseudo terminals. */
    strcat(strcpy(ptyErrS, ptyErr), "S");
    strcat(strcpy(ptyErrM, ptyErr), "M");
    strcat(strcpy(ptySysS, ptySys), "S");
    strcat(strcpy(ptySysM, ptySys), "M");

    /* Get rid of leading & trailing white space in place. */
    cMsgTrim(info->command);

    /* Get rid of any leading, trailing, or doubled semicolons in place. */
    cMsgTrimChar(info->command, ';');
    cMsgTrimDoubleChars(info->command, ';');
/*printf("trimmed command = %s\n", info->command);*/

    /* Multiple commands can be combined by placing semicolons between them.
     * Counting the semicolons now gives the number of commands in the string
     * (assuming command arguments do not contain semicolons). */
    cmdCount = strCharCount(info->command, ";") + 1;

    /*
     * Split up commands so we can execute them one-by-one.
     * This is done because we only know if a single shell command is done executing
     * by whether or not the shell prints out "value = ..." when it is finished.
     * If more than one command is executed at once and, for example, the first
     * one fails, we'll never know because we'll still be looking for the next
     * "value = " which will never come (it'll essentially hang).
     */
    commands = (char **) calloc(cmdCount, sizeof(char *));
    if (commands == NULL) {
        sendResponseMsg(domainId, responseMsg, "Cannot allocate memory", 1, 1);
        exit(-1);
    }
    pCmdCpy = strdup(info->command);
    commands[0] = strtok(pCmdCpy, ";");
    for (i=1; i<cmdCount; i++) {
        if ( (commands[i] = strtok('\0', ";")) == NULL ) {
            break;
        }
    }

    fdSlaveS  = open(ptySysS, O_RDWR, 0777);
    fdMasterS = open(ptySysM, O_RDWR, 0777);
    
    fdSlaveE  = open(ptyErrS, O_RDWR, 0777);
    fdMasterE = open(ptyErrM, O_RDWR, 0777);

    ioctl(fdSlaveS, FIOSETOPTIONS, OPT_TERMINAL);
    ioctl(fdSlaveE, FIOSETOPTIONS, OPT_TERMINAL);

    inBuf = (char *)calloc(1, bufSize);
    if (inBuf == NULL) {
        sendResponseMsg(domainId, responseMsg, "Cannot allocate memory", 1, 1);
        ptyDevRemove(ptyErr);
        ptyDevRemove(ptySys);
        exit(-1);
    }

    /* Start shell. */
    err = shellGenericInit("INTERPRETER=C", 0, NULL, &shellTaskName,
                            FALSE, FALSE, fdSlaveS, fdSlaveS, fdSlaveE);
    if (err == ERROR) {
        /* shell session cannot be created */
        sendResponseMsg(domainId, responseMsg, "Shell cannot be started", 1, 1);
        cMsgFreeMessage(&responseMsg);
        ptyDevRemove(ptyErr);
        ptyDevRemove(ptySys);
        free(pCmdCpy); free(commands); free(inBuf);
        free(info->commander); free(info->command); free(info);
        return;
    }
/*printf("Shell name = %s\n", shellTaskName);*/
    
    /* Wait until shell shows up in table. */
    while ( (info->shellId = taskNameToId(shellTaskName)) == ERROR) {
        taskDelay(.1*sysClkRateGet()); /* wait 0.1 sec */
        if (++timeCount >= 20) {
            sendResponseMsg(domainId, responseMsg, "Shell cannot be started", 1, 1);
            cMsgFreeMessage(&responseMsg);
            ptyDevRemove(ptyErr);
            ptyDevRemove(ptySys);
            free(pCmdCpy); free(commands); free(inBuf);
            free(info->commander); free(info->command); free(info);
            return;
        }
    }

    err = CMSG_OK;
    delayTicks = sysClkRateGet()/10; /* 0.1 sec */

    /* for each command ... */
    for (i=0; i <cmdCount; i++) {

        /* Send command to shell. */
/*printf("Write %s to shell\n", commands[i]); */
        write(fdMasterS, commands[i], strlen(commands[i]));
        write(fdMasterS, "\n", strlen("\n"));

        /* Set variables */
        memset(inBuf, 0, bufSize);
        pBuf = inBuf;
        maxReadSize = bufSize;
        totalBytesRead = 0;
        errorStr = NULL;

        /* Trying reading any error output first since usually that happens right away ... */
        while (1) {
            /* Delay 0.1 sec */
            taskDelay(delayTicks);
    
            /* Are there any error bytes to be read? */
            rc = ioctl(fdSlaveE, FIONWRITE, (int)&bytesUnread);
            if (rc == ERROR) {
                errorStr = "ioctl error";
                break;
            }
    
            if (bytesUnread < 1) {
                /* If there was an error output ... */
                if (totalBytesRead > 0) {
/*printf("  NO more error bytes to read\n");*/
                    /* Get rid of newlines before and aft. */
                    cMsgTrim(inBuf);
                    errorStr = inBuf;
                    break;
                }
                else {
/*printf("  NO error bytes to read\n");*/
                }
                break;
            }
      
            /* if we have more to read than will fit in our buffer ... */
            if  ( (totalBytesRead + bytesUnread) > bufSize ) {
                /* increase buffer size */
                bufSize = totalBytesRead + bytesUnread + 1024;
                inBuf = upBufferSize(inBuf, totalBytesRead, bufSize);
                maxReadSize = bytesUnread + 1024;
                pBuf = inBuf + totalBytesRead;
/*printf("Reset buf size to %d\n", bufSize);*/
            }
     
            /* read everything we can */
            bytesRead = read(fdMasterE, pBuf, maxReadSize);
            if (bytesRead == ERROR) {
                /* pseudo-terminal driver error */
                errorStr = "pty driver error";
                break;
            }
/*printf("Error output = %s\n",inBuf);*/

            pBuf += bytesRead;
            totalBytesRead += bytesRead;
            /* Make sure we don't overflow our read buffer. */
            maxReadSize = bufSize - totalBytesRead;
        }

        
        /* If we had an error ... */
        if (errorStr != NULL) {
            /* If we've exceeded the time limit, this process was put in the hash
             * table (inHash == true), and any error is no longer "immediate".
             * If it's not immediate, we have to do more bookkeeping - can't
             * just return a single msg. */
            if (inHash) {
                cMsgAddString(responseMsg, "error", errorStr);
                goto terminated;
            }
            else {
                sendResponseMsg(domainId, responseMsg, errorStr, 1, 1);
                cMsgFreeMessage(&responseMsg);
                ptyDevRemove(ptyErr);
                ptyDevRemove(ptySys);
                free(pCmdCpy); free(commands); free(inBuf);
                free(info->commander); free(info->command); free(info);
                return;
            }
        }

        
        /* reset variables for reading regular output */
        memset(inBuf, 0, bufSize);
        pBuf = inBuf;
        maxReadSize = bufSize;
        totalBytesRead = 0;
        errorStr = NULL;
     

        /* While command not finished running ... */
        while (1) {
            /* Enough time elapsed, so store in hash table. */
            if (!inHash) {
                id = getUniqueId();
                err = cMsgAddInt32(responseMsg, "id", id);
                if (err != CMSG_OK) {
                    sendResponseMsg(domainId, responseMsg, "Cannot allocate memory", 1, 1);
                    ptyDevRemove(ptyErr);
                    ptyDevRemove(ptySys);
                    exit(-1);
                }

                processMapPut(idTable, id, info);
                inHash = 1;
            }

            /* If the Commander is using a callback (not waiting for results synchronously),
             * then return an immediate synchronous sendAndGet reply msg so as not to hold
             * the Commander up since we'll send results later - to the callback. */
            if (!info->wait && !sentNotWaitingMsg) {
                sendResponseMsg(domainId, responseMsg, NULL, 0, 0);
                sentNotWaitingMsg = 1;
            }
            
            /* Delay 0.1 sec */
            taskDelay(delayTicks);
            
            /* Check to see if we've been told to quit. If so, we are by
             * definition, terminated. Grab mutex to ensure latest value
             * for info structure is seen. */
            status = pthread_mutex_lock(&idMutex);
            if (status != 0) {
                cmsg_err_abort(status, "Failed processThread mutex lock");
            }

            if (info->killed || info->stopped) {
                if (info->killed) {
                    cMsgAddInt32(responseMsg, "killed", 1);
                }
                else if (info->stopped) {
                    cMsgAddInt32(responseMsg, "stopped", 1);
                }

                status = pthread_mutex_unlock(&idMutex);
                if (status != 0) {
                    cmsg_err_abort(status, "Failed flag mutex unlock");
                }
/*printf("Been told to stop/kill\n");*/
                
                /* Being told to die can only happen if we are already in the hash table! */
                goto terminated;
            }
            
            status = pthread_mutex_unlock(&idMutex);
            if (status != 0) {
                cmsg_err_abort(status, "Failed processThread mutex unlock");
            }

            /* Are there any bytes to be read yet? */
            rc = ioctl(fdSlaveS, FIONWRITE, (int)&bytesUnread);
            if (rc == ERROR) {
                /* Internal error, bad file descriptor */
                errorStr = "ioctl error 2";
                break;
            }
    
            /* If no bytes to read, wait some more ... */
            if (bytesUnread < 1) {
/*printf("    There are no unread bytes\n");*/
                continue;
            }
/*printf("    There are %d unread bytes\n", bytesUnread);*/
    
            /* If we have more to read than will fit in our buffer ... */
            if  ( (totalBytesRead + bytesUnread) > bufSize ) {
                /* increase buffer size */
                bufSize = totalBytesRead + bytesUnread + 1024;
                inBuf = upBufferSize(inBuf, totalBytesRead, bufSize);
                maxReadSize = bytesUnread + 1024;
                pBuf = inBuf + totalBytesRead;
            }
     
            /* Read everything we can. */
            bytesRead = read(fdMasterS, pBuf, maxReadSize);
            if (bytesRead == ERROR) {
                errorStr = "cmd output reading error";
                break;
            }

            pBuf += bytesRead;
            totalBytesRead += bytesRead;
            /* Make sure we don't overflow our read buffer. */
            maxReadSize = bufSize - totalBytesRead;
    
            /* Are we done with the command(s), or must we wait some more?
             * The vxworks shell prints out the string "value = ..."
             * at the end of each command. So we count these to make
             * sure we have one for each command.
             */
            valueCount = strStringCount(inBuf, "value = ");
/*printf("\"value =\" count = %d\n", valueCount);*/
            if (valueCount > 0) {
                /* If Commander is interested in the output, store it. */
                if (info->monitor) {
                    int len=0, addingOn=0;
                    char *pChar, *oldString=NULL;

                    /* We're done with command so extract output. */
                    pBuf = removeCmdEcho(inBuf, commands[i]);
                    
                    /* Remove "value = " & following from end of buffer. */
                    removeLastValueEquals(pBuf);

                    /* Get rid of newlines before and aft. */
                    cMsgTrim(pBuf);

                    /* If there's no text left, there was no real output. */
                    if (strlen(pBuf) < 1) {
                        break;
                    }

                    /* Store output by appending to any existing output from previous commands. */
                    err = cMsgGetString(responseMsg, "output", &val);
                    /* If we have previously stored results ... */
                    if (err == CMSG_OK) {
                        /* Add one cause we're going to put newline at end since we're
                           concatenating output from multiple commands. */
                        len += strlen(val) + 1;
                        /* copy string from payload because we're going to free it next */
                        oldString = strdup(val);
                        cMsgPayloadRemove(responseMsg, "output");
                        addingOn = 1;
                    }
                    len += strlen(pBuf) + 1;

                    pChar = (char *) calloc(1,len);
                    if (pChar == NULL) {
                        if (!inHash) {
                            sendResponseMsg(domainId, responseMsg, "Cannot allocate memory", 1, 1);
                        }
                        ptyDevRemove(ptyErr);
                        ptyDevRemove(ptySys);
                        exit(-1);
                    }

                    if (addingOn) {
                        strcat(pChar, oldString);
                        strcat(pChar, "\n");
                        free(oldString);
                    }
                    strcat(pChar, pBuf);

                    err = cMsgAddString(responseMsg, "output", pChar);
                    free(pChar);
                    if (err != CMSG_OK) {
                        if (!inHash) {
                            sendResponseMsg(domainId, responseMsg, "Cannot allocate memory", 1, 1);
                        }
                        ptyDevRemove(ptyErr);
                        ptyDevRemove(ptySys);
                        exit(-1);
                    }
/*printf("Command output = %s\n",pChar);*/
                }

                break;
            }
        }
        

        /* If we had an error with running command ... */
        if (errorStr != NULL) {
            if (inHash) {
                cMsgAddString(responseMsg, "error", errorStr);
                goto terminated;
            }
            else {
                sendResponseMsg(domainId, responseMsg, errorStr, 1, 1);
                cMsgFreeMessage(&responseMsg);
                ptyDevRemove(ptyErr);
                ptyDevRemove(ptySys);
                free(pCmdCpy); free(commands); free(inBuf);
                free(info->commander); free(info->command); free(info);
                return;
            }
        }


    } /* for each cmd */

    /* At this point, our command(s) has run successfully or was terminated either
     * because an error occurred or the Commander sent a "stop" or "kill" message.
     * Any asynchronous Commander method has been replied to. The only thing
     * remaining is to reply to a synchronous method and send a message to any
     * callback waiting to hear from this finished process. */
    terminated:

    if (inHash) processMapRemove(idTable, id);

    err = cMsgAddInt32(responseMsg, "terminated", 1);
    if (err != CMSG_OK) {
        /* Cannot allocate memory */
        ptyDevRemove(ptyErr);
        ptyDevRemove(ptySys);
        exit(-1);
    }
         
    close(fdSlaveS);
    close(fdMasterS);

    close(fdSlaveE);
    close(fdMasterE);

    free(inBuf);
    free(pCmdCpy);
    free(commands);

    if ( (shellId = taskNameToId(shellTaskName)) != ERROR ) {
        /*printf("Terminated Shell by Hand\n");*/
        shellTerminate(shellId);
    }

    /*----------------------------------------------------------------
    // Respond to initial sendAndGet if we haven't done so already
    // so "startProcess" can return if it has not timed out already
    // (in which case it is not interested in this result anymore).
    //----------------------------------------------------------------*/
    if (info->wait) {
/*printf("Sending msg to Cmdr if it's waiting ......\n");*/
        cMsgSend(domainId, responseMsg);
        cMsgFreeMessage(&responseMsg);
        ptyDevRemove(ptyErr);
        ptyDevRemove(ptySys);
        free(info->commander); free(info->command); free(info);
        return;
    }

    /*----------------------------------------------------------------
    // If we're here it's because the Commander is not synchronously
    // waiting, but has registered a callback that the next message
    // will trigger.
    //
    // Now send another (regular) msg back to Commander to notify it
    // that the process is done and run any callback associated with
    // this process. As part of that, the original CommandReturn object
    // will be updated with the following information and given as an
    // argument to the callback.
    //----------------------------------------------------------------*/
    imDoneMsg = cMsgCreateMessage();
    if (imDoneMsg == NULL) {
        /* Cannot allocate memory */
        ptyDevRemove(ptyErr);
        ptyDevRemove(ptySys);
        exit(-1);
    }
    
    cMsgSetSubject(imDoneMsg, info->commander);
    cMsgSetType(imDoneMsg, remoteExecSubjectType);
        
    err = cMsgAddString(imDoneMsg, "returnType", "process_end");
    if (err != CMSG_OK) {
        ptyDevRemove(ptyErr);
        ptyDevRemove(ptySys);
        exit(-1);
    }

    err = cMsgAddInt32(imDoneMsg, "id", info->commandId);
    if (err != CMSG_OK) {
        ptyDevRemove(ptyErr);
        ptyDevRemove(ptySys);
        exit(-1);
    }

    /* copy over any output string from the response msg */
    if (info->monitor) {
        err = cMsgGetString(responseMsg, "output", &val);
        if (err == CMSG_OK) {
            err = cMsgAddString(imDoneMsg, "output", val);
            if (err != CMSG_OK) {
                ptyDevRemove(ptyErr);
                ptyDevRemove(ptySys);
                exit(-1);
            }
        }
    }

    /* copy over any error string from the response msg */
    err = cMsgGetString(responseMsg, "error", &val);
    if (err == CMSG_OK) {
        err = cMsgAddString(imDoneMsg, "error", val);
        if (err != CMSG_OK) {
            ptyDevRemove(ptyErr);
            ptyDevRemove(ptySys);
            exit(-1);
        }
    }

    if (info->killed) {
        err = cMsgAddInt32(imDoneMsg, "killed", 1);
        if (err != CMSG_OK) {
            ptyDevRemove(ptyErr);
            ptyDevRemove(ptySys);
            exit(-1);
        }
    }
    else if (info->stopped) {
        err = cMsgAddInt32(imDoneMsg, "stopped", 1);
        if (err != CMSG_OK) {
            ptyDevRemove(ptyErr);
            ptyDevRemove(ptySys);
            exit(-1);
        }
    }

/*printf("Sending msg to run callback for Cmdr ......\n");*/
    cMsgSend(domainId, imDoneMsg);

    cMsgFreeMessage(&imDoneMsg);
    cMsgFreeMessage(&responseMsg);
    free(info->commander); free(info->command); free(info);
    ptyDevRemove(ptyErr);
    ptyDevRemove(ptySys);
}

/******************************************************************/

#endif
