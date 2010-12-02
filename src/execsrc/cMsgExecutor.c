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
 * to run on its host. The Executor runs it and reports the results back to the
 * Commander. This code is designed to run on VXWORKS only.
 */

#ifdef VXWORKS
#include <vxWorks.h>
#include <taskLib.h>
#include <symLib.h>
#include <symbol.h>
#include <sysSymTbl.h>
#else
#include <sys/utsname.h>
#endif

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

#if !defined linux || !defined _GNU_SOURCE
    char *strndup(const char *s, size_t n);
#endif

/** This structure stores information about a received command. */
typedef struct commandInfo_t {
    char *className;
    char *command;
    char *commander;
    int   commandId;
    int   monitor;
    int   wait;
    int   isProcess;
    int   process;
    int   killed;  /* volatile */
    int   stopped; /* volatile */
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
    info->isProcess   = 0;
    info->process     = 0;
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
} passedArg;



/** cMsg message subject or type used in internal Commander/Executor communication. */
static char* allSubjectType = ".all";

/** cMsg message subject or type used in internal Commander/Executor communication. */
static char* remoteExecSubjectType = "cMsgRemoteExec";

/** Pthread mutex serializing calls to getUniqueId(). */
static pthread_mutex_t idMutex = PTHREAD_MUTEX_INITIALIZER;

/** Unique id to assign incoming requests. */
static int uniqueId = 1;

/* prototypes of static functions */
static void  processMapPut(hashTable *pTable, int id, void *info);
static void  processMapRemove(hashTable *pTable, int id);
static int   getUniqueId();
static int   sendStatusTo(void *domainId, void *msg, const char *subject);
static void *createStatusMessage(const char *name, const char *host);
static char *decryptPassword(const char *string, int len);
static void  callback(void *msg, void *arg);
static void *processThread(void *arg);
static void  stop(int id, hashTable *pTable);
static void  stopAll(int kill, hashTable *pTable);

int executorMain(char *udl, char *password, char *name);


/** AES encryption key. */
static unsigned char AESkey[16] = {-121, -59, -26,  12,
                                    -51, -29, -26,  86,
                                    110,  25, -23, -27,
                                    112, -80,  77, 102 };



//void stuff() {
//    char workName[16];             /* name of work task */
//    sprintf (workName, "tTcpWork%d", ix++);
//
//    if (taskSpawn(workName, SERVER_WORK_PRIORITY, 0, SERVER_STACK_SIZE, (FUNCPTR) tcpServerWorkTask, 
//        newFd, cmd, 0, 0, 0, 0, 0, 0, 0, 0) == ERROR)  {
//        /* if taskSpawn fails, close fd and return to top of loop */
//        perror ("taskSpawn");
//        close (newFd);
//   }
//}

/**************************************************************************** 
* * tcpServerWorkTask - process client requests 
* * This routine reads from the server's socket, and processes client 
* requests. If the client requests a reply message, this routine 
* will send a reply to the client. 
* * RETURNS: N/A. */ 

//void tcpServerWorkTask(int sFd, static char *cmd)
//{
//    /* Set IO redirection. */
//    ioTaskStdSet(0, STD_ERR, sFd);    /* set std err for execute() */
//    ioTaskStdSet(0, STD_OUT, sFd);    /* set std out for execute() */
//    
//    /* try Executing the message */
//    execute(cmd);
//
//    fflush(stderr);
//    fflush(stdout);
//
//    close (sFd);    /* close server socket connection */
//    return;
//}
/******************************************************************/
                          
/**
 * Prints the proper usage of this executable.
 */
static void usage() {
    printf("\nUsage:  cMsgExecutor -u <UDL> [-d | -n <name> | -p <password>]\n\n");
    printf("                     -u    sets the cmsg connection UDL\n");
    printf("                     -d    turns on debug output\n");
    printf("                     -n    sets the client name\n");
    printf("                     -p    sets the password\n");
    printf("                     -h... prints this output\n");
    printf("\n");
}


/**
 * This routine puts a key-value pair into a hash table.
 * 
 * @param pTable pointer to hash table
 * @param id key (first converted to string)
 * @param info value
 */
static void processMapPut(hashTable *pTable, int id, void *info) {
    char key[10];
    
    /* change id into string so it can be hash table key */
    sprintf(key, "%d", id);

    hashInsert(pTable, key, (void *)info, NULL);
}


/**
 * This routine removes an entry from a hash table.
 *
 * @param pTable pointer to hash table
 * @param id key of entry to remove
 */
static void processMapRemove(hashTable *pTable, int id) {
    char key[10];
    
    /* change id into string so it can be hash table key */
    sprintf(key, "%d", id);

    hashRemove(pTable, key, NULL);
}


/**
 * This routine generates and returns the next unique id number.
 * 
 * @return unique id number
 */
static int getUniqueId() {
    int id, status;
    
    status = pthread_mutex_lock(&idMutex);
    if (status != 0) {
        cmsg_err_abort(status, "Failed getUniqueId mutex lock");
    }
    
    id = uniqueId++;
    
    status = pthread_mutex_unlock(&idMutex);
    if (status != 0) {
        cmsg_err_abort(status, "Failed getUniqueId mutex unlock");
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
    printf("sendStatusTo: %s\n", subject);
    cMsgSetSubject(msg, subject);
    return cMsgSend(domainId, msg);
}


#ifndef VXWORKS


/**
 * Main executable function. Not used in vxworks.
 * 
 * @param argc number of arguments
 * @param argv array of string arguments
 * @return exit status
 */
int main(int argc,char **argv) {

    int i, debug = 0;
    char *myUDL      = NULL;
    char *myName     = NULL;
    char *myPassword = NULL;

    if (argc > 1) {
        for (i=1; i<argc; i++) {
            if (strcmp(argv[i], "-n") == 0) {
                if (argc < i+2) {
                    usage();
                    return(-1);
                }
                myName = argv[++i];
            }
            else if (strcmp(argv[i], "-u") == 0) {
                if (argc < i+2) {
                    usage();
                    return(-1);
                }
                myUDL = argv[++i];
            }
            else if (strcmp(argv[i], "-p") == 0) {
                if (argc < i+2) {
                    usage();
                    return(-1);
                }
                myPassword = argv[++i];
            }
            else if (strcmp(argv[i], "-d") == 0) {
                debug = 1;
            }
            else if (strncmp(argv[i], "-h", 2) == 0) {
                usage();
                return(-1);
            }
            else {
                usage();
                return(-1);
            }
        }
    }

    /* UDL is required since we need to connect to the cMsg server. */
    if (myUDL == NULL) {
        printf("\nThe UDL is a required argument\n");
        usage();
        return(-1);
    }
    
    executorMain(myUDL, myPassword, myName);
}


#endif


/**
 * Main function for running Executor. Use this as "main" for vxworks.
 * 
 * @param udl      udl for connecting to cMsg server
 * @param password password Commander needs to provide for
 *                 this Executor to execute any commands.
 * @param name     name for cMsg connection
 * @return         exit status
 */
int executorMain(char *udl, char *password, char *name) {
    char *myDescription = "cmsg C executor";
    char  host[CMSG_MAXHOSTNAMELEN];
    int   err, connected = 0;
    void *domainId, *unsubHandle1, *unsubHandle2, *statusMsg;
    cMsgSubscribeConfig *config;
    passedArg args;
    struct timespec wait = {1, 0}; /* 1 sec */
    /* Hash table with key = unique id (as string) and value = commandInfo. */
    hashTable idTable;
    
    hashInit(&idTable,  64);

    /* Limit length of password to 16 chars. */
    if (password != NULL) {
        if (strlen(password) > 16) {
            printf("executorMain: password must not be more than 16 characters\n");
            exit(1);
        }
    }

    /* Create subscription configuration. */
    config = cMsgSubscribeConfigCreate();
    if (config == NULL) {
        printf("executorMain: cannot allocate memory\n");
        exit(1);
    }
   
    /* Find the name of the host we're running on. */
    err = cMsgLocalHost(host, CMSG_MAXHOSTNAMELEN);
    if (err != CMSG_OK) {
        printf("executorMain: cannot get host name\n");
        exit(1);
    }

    /* If Executor is not given a name, use the host as its name. */
    if (name == NULL) {
        name = host;
    }

    printf("Running Executor %s\n", name);

    /* Create a status message to send to Commanders telling them about this Executor. */
    statusMsg = createStatusMessage(name, host);
    if (statusMsg == NULL) {
        printf("executorMain: cannot allocate memory\n");
        exit(1);
    }
      
    /* Make a connection to a cMsg server or keep trying to connect. */
    while (1) {
        
        /* 1 sec delay */
        nanosleep(&wait, NULL);

        if (!connected) {
            
printf("Try to (re)connect\n");

            /* connect */
            err = cMsgConnect(udl, name, myDescription, &domainId);
            if (err != CMSG_OK) {
            }

            cMsgReceiveStart(domainId);
            config = cMsgSubscribeConfigCreate();

            args.arg1 = (void *)password;
            args.arg2 = (void *)&domainId;
            args.arg3 = statusMsg;
            args.arg4 = &idTable;

            /* add subscriptions */
            err = cMsgSubscribe(domainId, remoteExecSubjectType, name, callback,
                                (void *)&args, config, &unsubHandle1);
            if (err != CMSG_OK) {
                printf("executorMain: %s\n",cMsgPerror(err));
                exit(1);
            }

            err = cMsgSubscribe(domainId, remoteExecSubjectType, allSubjectType,
                                callback, (void *)&args, config, &unsubHandle2);
            if (err != CMSG_OK) {
                printf("executorMain: %s\n",cMsgPerror(err));
                exit(1);
            }

            /* Send out message telling all commanders that there is a new executor running. */
            sendStatusTo(domainId, statusMsg, allSubjectType);
        }

        /* Are we (still) connected to the cMsg server? */
        err = cMsgGetConnectState(domainId, &connected);
        if (err != CMSG_OK) {
            connected = 0;
        }
    }
      
    cMsgDisconnect(&domainId);
    hashDestroy(&idTable, NULL, NULL);

    return(0);
}


/**
 * Create a status message sent out to all commanders as soon
 * as we connect to the cMsg server and to all commanders who
 * specifically ask for it.
 *
 * @param name name of this Executor.
 * @param host host this Executor is running on.
 * @return cMsg status message or NULL if memory cannot be allocated
 */
static void *createStatusMessage(const char *name, const char *host) {

    int err;
    char *os, *machine, *processor, *release;
    void *statusMsg = cMsgCreateMessage();
#ifndef VXWORKS
    struct utsname myname;
#endif

    if (statusMsg == NULL) {
        return NULL;
    }
    
    cMsgSetHistoryLengthMax(statusMsg, 0);

    /* The subject of this msg gets changed depending on who it's sent to. */
    cMsgSetSubject(statusMsg, allSubjectType);
    cMsgSetType(statusMsg, remoteExecSubjectType);

    err = cMsgAddString(statusMsg, "returnType", "reporting");
    if (err != CMSG_OK) {
        /* only possible error at this point */
        printf("Reject message, cannot allocate memory");
        exit(-1);
    }
    
    err = cMsgAddString(statusMsg, "name", name);
    if (err != CMSG_OK) {
        /* only possible error at this point */
        printf("Reject message, cannot allocate memory");
        exit(-1);
    }
    
    err = cMsgAddString(statusMsg, "host", host);
    if (err != CMSG_OK) {
        /* only possible error at this point */
        printf("Reject message, cannot allocate memory");
        exit(-1);
    }
    
#ifdef VXWORKS

    os        = "vxworks";
    release   = "6.0";
    machine   = "unknown";
    processor = "unknown";
    // look at version()

#else

    /* find out the name of the machine we're on */
    if (uname(&myname) < 0) {
        os        = "unknown";
        release   = "unknown";
        machine   = "unknown";
        processor = "unknown";
    }
    else {       
        os        = myname.sysname;
        release   = myname.release;
        machine   = myname.machine;
        processor = "unknown";
    }

#endif

    err = cMsgAddString(statusMsg, "os", os);
    if (err != CMSG_OK) {
        /* only possible error at this point */
        printf("Reject message, cannot allocate memory");
        exit(-1);
    }
    
    err = cMsgAddString(statusMsg, "machine", machine);
    if (err != CMSG_OK) {
        /* only possible error at this point */
        printf("Reject message, cannot allocate memory");
        exit(-1);
    }
    
    err = cMsgAddString(statusMsg, "processor", processor);
    if (err != CMSG_OK) {
        /* only possible error at this point */
        printf("Reject message, cannot allocate memory");
        exit(-1);
    }

    err = cMsgAddString(statusMsg, "release", release);
    if (err != CMSG_OK) {
        /* only possible error at this point */
        printf("Reject message, cannot allocate memory");
        exit(-1);
    }
    
    return statusMsg;
}


/**
 * Decrypt the given string into a recognizable password.
 * This routine allocates memory for the returned string
 * which must be freed by the caller.
 * 
 * @param string input string
 * @param len length (number of characters) in non-encrypted password
 * @return password from Commander
 */
static char *decryptPassword(const char *string, int len) {
    char pswrd[16];
    char *bytes;
    aes_context ctx;
    unsigned int bytesLen;
    int numBytes;
    
    memset(pswrd,0,16);

    /* number of bytes in decoded B64 string */
    bytesLen = cMsg_b64_decode_len(string, strlen(string));
    bytes = (char *) calloc(1, bytesLen);
    if (bytes == NULL) {
        printf("decryptPassword: cannot allocate memory");
        exit(-1);
    }
    
    /* string is in B64 form, decode to byte array */
    numBytes = cMsg_b64_decode(string, strlen(string), bytes);

    /* Initialize context structure. */
    aes_setkey_dec(&ctx, AESkey, 128);

    /* Decrypt bytes into actual password. */
    aes_crypt_ecb(&ctx, AES_DECRYPT, bytes, pswrd);

    free(bytes);
    return strndup(pswrd, len);
}


/**
 * This routine defines the callback to be run when a message
 * containing a valid command arrives.
 */
static void callback(void *msg, void *arg) {

    int err, status, payloadCount;
    pthread_t tid;
    passedArg *args2, *args = (passedArg *)arg;
    /*
     * The domain id may change if a new cMsg connection is made.
     * Thus, we passed the pointer to the domain Id so it can be
     * refreshed each time this callback is run.
     */
    void *domainId;
    char *password;
    void *statusMsg;
    hashTable *pTable;

    domainId  = (void *) *((void **)args->arg2);
    password  = (char *)args->arg1;
    statusMsg = args->arg3;
    pTable    = (hashTable *)args->arg4;

    /* There must be a payload. */
    cMsgHasPayload(msg, &payloadCount);
    if (payloadCount > 0) {

        int32_t intVal;
        const char *val;
        char *passwd = NULL;
        const char *commandType;

        err = cMsgGetString(msg, "p", &val);
        if (err == CMSG_OK) {
            int32_t pswdLen;
            err = cMsgGetInt32(msg, "pl", &pswdLen);
            if (err != CMSG_OK) {
                printf("Reject message, no password length");
                cMsgFreeMessage(&msg);
                return;
            }
            
            /* decrypt password here */
            passwd = decryptPassword(val, pswdLen);
printf("Decrypted password -> -----%s-----\n", passwd);
        }

        /* check password if required */
        if (password != NULL && strcmp(password, passwd) != 0) {
            cMsgFreeMessage(&msg);
            if (passwd != NULL) {
                free(passwd);
            }
            return;
        }

        if (passwd != NULL) {
            free(passwd);
        }

        /* What command are we given? */
        err = cMsgGetString(msg, "commandType", &commandType);
        if (err != CMSG_OK) {
printf("Reject message, no command type\n");
            cMsgFreeMessage(&msg);
            return;
        }
            
printf("commandtype = %s\n", commandType);

        if (strcmp(commandType, "start_process") == 0) {
            /* Store incoming data here */
            int monitor, wait, isGetRequest;
            passedArg *arg;
            void *responseMsg;
            commandInfo *info;
             
            /* Is the msg from a sendAndGet? */
            isGetRequest = 0;
            cMsgGetGetRequest(msg, &isGetRequest);
            if (!isGetRequest) {
                printf("Reject message, start_process cmd must be sendAndGet msg");
                cMsgFreeMessage(&msg);
                return;
            }

            info = (commandInfo *) malloc(sizeof(commandInfo));
            if (info == NULL) {
                printf("Reject message, cannot allocate memory");
                exit(-1);
            }
            initCommandInfo(info);
            info->isProcess = 1;

            err = cMsgGetString(msg, "command", &val);
            if (err != CMSG_OK) {
                printf("Reject message, no command");
                cMsgFreeMessage(&msg);
                return;
            }
            info->command = strdup(val);

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
                printf("Reject message, no commander");
                cMsgFreeMessage(&msg);
                return;
            }
            info->commander = strdup(val);

            err = cMsgGetInt32(msg, "id", &intVal);
            if (err != CMSG_OK) {
                printf("Reject message, no commander id");
                cMsgFreeMessage(&msg);
                return;
            }
            info->commandId = intVal;

            /* Return must be placed in sendAndGet response msg. */
            responseMsg = cMsgCreateResponseMessage(msg);

            /* Create arg to new thread. */
            args2 = (passedArg *) malloc(sizeof(passedArg));
            args2->arg1 = (void *)info;
            args2->arg2 = responseMsg;
            args2->arg3 = domainId;
            args2->arg4 = args->arg4; /* pass along hash table pointer */

            /* Start up new thread. */
printf("Starting process thread\n");
            status = pthread_create(&tid, NULL, processThread, (void *)args2);
            if (status != 0) {
                printf("Error creating update server thread");
                exit(-1);
            }
        }
        else if (strcmp(commandType, "start_thread") == 0) {
            /* todo: we should send back an error here */
            printf("Reject message, start_thread cmd is not supported on vxworks");
            cMsgFreeMessage(&msg);
            return;
        }
        else if (strcmp(commandType, "stop_all") == 0) {
            stopAll(0, pTable);
        }
        else if (strcmp(commandType, "stop") == 0) {
            err = cMsgGetInt32(msg, "id", &intVal);
            if (err != CMSG_OK) {
                printf("Reject message, no id");
                cMsgFreeMessage(&msg);
                return;
            }
            stop(intVal, pTable);
        }
        else if (strcmp(commandType, "die") == 0) {
            int killProcesses = 0;
            err = cMsgGetInt32(msg, "killProcesses", &intVal);
            if (err == CMSG_OK) {
                killProcesses = intVal;
            }

            if (killProcesses) stopAll(1, pTable);
            exit(0);
        }
        else if (strcmp(commandType, "identify") == 0) {
            err = cMsgGetString(msg, "commander", &val);
            if (err != CMSG_OK) {
                printf("Reject message, no commander");
                cMsgFreeMessage(&msg);
                return;
            }

            sendStatusTo(domainId, statusMsg, val);
        }
        else {
            printf("Reject message, invalid command");
        }
    }
    else {
        printf("Reject message, no payload");
    }

    cMsgFreeMessage(&msg);
}

static int processTerminated(int i) {return i;}

static void gatherAllOutput(int process, void *responseMsg, int monitor) {
    struct timespec wait = {0, 200000000}; /*0. 2 sec */
    /* 0.2 sec delay */
printf("Gather all output\n");
    nanosleep(&wait, NULL);
}


/**
 * Routine to stop the given command.
 * @param id id of command to stop
 */
static void stop(int id, hashTable *pTable) {
    char key[10];
    commandInfo *info;
  
    /* change id into string so it can be hash table key */
    sprintf(key, "%d", id);

    if (!hashLookup(pTable, key, (void **)&info)) {
        return;
    }

    // stop process
    if (info->isProcess) {
        info->stopped = 1;
printf("stop(): stop id = %d\n", id);
        //info.process.destroy();
    }

}


/**
 * Routine to stop all commands.
 */
static void stopAll(int kill, hashTable *pTable) {

    int i, size;
    hashNode *entries;
    commandInfo *info;
    
    if (hashGetAll(pTable, &entries, &size)) {
        for (i=0; i < size; i++) {
            info = (commandInfo *)entries[i].data;
            if (kill) {
                info->killed = 1;
            }
            else {
                info->stopped = 1;
            }

            // stop process
printf("stopAll(): stop id = %s\n", entries[i].key);
          //info->process.destroy();
        }
    }

    free(entries);
}



/**
 * Thread to run command in.
 * @param arg pointer to structure holding arguments.
 */
static void *processThread(void *arg) {

    passedArg *args    = (passedArg *) arg;
    commandInfo *info  = (commandInfo *)args->arg1;
    void *responseMsg  = args->arg2;
    void *domainId     = args->arg3;
    hashTable *idTable = (hashTable *)args->arg4;

    void *imDoneMsg;
    char *stringsOut[2];
    int id = 0, err, terminated;
    struct timespec wait = {0, 100000000}; /* 0.1 sec */

    int fdSlave, fdMaster;
    char *shellTaskName;
    char inBuf[512];
    memset(inBuf, 0, 512);

    printf("Run fake process thread with command -> %s\n", info->command);
    printf("Sizeof(inBuf) -> %d\n", (int)sizeof(inBuf));


    //----------------------------------------------------------------
    // run the command HERE
    //----------------------------------------------------------------
    err = CMSG_OK; /* run command here */
#ifdef VXWORKS
    ptyDevCreate("system.", 512, 512);
    fdSlave  = open("system.S", O_RDWR, 0644);
    fdMaster = open("system.M", O_RDWR, 0644);
    shellGenericInit("INTERPRETER=Cmd", 0, NULL, &shellTaskName, FALSE, FALSE, fdSlave, fdSlave, fdSlave);
    taskDelay(sysClkRateGet());
    write(fdMaster, info->command, strlen(info->command));
    err = read(fdMaster, inBuf, 512);
    if (err == ERROR) {
        printf("Error reading command output\n");
    }
    else {
        printf("Command output = %s\n",inBuf);
    }
    close(fdMaster);
    close(fdSlave);
#endif

    /* Allow process a chance to run before testing if its terminated. */
    //sched_yield();
    
    /* 0.1 sec delay */
    //nanosleep(&wait, NULL);
    
    /* Return error message if execution failed. */
    if (err != CMSG_OK) {
        err = cMsgAddInt32(responseMsg, "terminated", 1);
        if (err != CMSG_OK) {
            /* only possible error at this point */
            printf("Cannot allocate memory 1\n");
            exit(-1);
        }
    
        err = cMsgAddString(responseMsg, "error", "some error");
        if (err != CMSG_OK) {
            printf("Cannot allocate memory 2\n");
            exit(-1);
        }
    
        err = cMsgAddInt32(responseMsg, "immediateError", 1);
        if (err != CMSG_OK) {
            printf("Cannot allocate memory 3\n");
            exit(-1);
        }
    
printf("Send a response msg to command\n");

        err = cMsgSend(domainId, responseMsg);
        if (err != CMSG_OK) {
            printf("PROBLEMS sending sendAndGet return msg -> %s\n", cMsgPerror(err));
        }
        return;
    }

    //---------------------------------------------------------
    // Figure out if process has already terminated.
    //---------------------------------------------------------
    terminated = processTerminated(0);

    //----------------------------------------------------------------
    // If process is NOT terminated, then put this process in
    // hash table storage so it can be terminated later.
    //----------------------------------------------------------------
    if (!terminated) {
        int process = 5;
        
        id = getUniqueId();
        err = cMsgAddInt32(responseMsg, "id", id);
        if (err != CMSG_OK) {
            printf("Cannot allocate memory 5\n");
            exit(-1);
        }

        info->process = process;
        processMapPut(idTable, id, info);
    }

    //----------------------------------------------------------------
    // If commander NOT waiting for process to finish,
    // send return message at once.
    //----------------------------------------------------------------
    if (!info->wait) {
        int process = 1;

        // Grab any output available if process already terminated.
        if (terminated) {
            err = cMsgAddInt32(responseMsg, "terminated", 1);
            if (err != CMSG_OK) {
                printf("Cannot allocate memory 4\n");
                exit(-1);
            }

            // grab any output and put in response message
            gatherAllOutput(process, responseMsg, info->monitor);
        }

        // Send response to Commander's sendAndGet.
printf("SENDING MSG TO CMDR FOR NON-WAITER ......\n");
        cMsgSend(domainId, responseMsg);

        // Now, finish waiting for process and notify Commander when finished.
    }

    //---------------------------------------------------------
    // If we're here, we want to wait until process is fnished.
    //---------------------------------------------------------
    cMsgPayloadRemove(responseMsg, "terminated");
    err = cMsgAddInt32(responseMsg, "terminated", 1);
    if (err != CMSG_OK) {
        printf("Cannot allocate memory 6\n");
        exit(-1);
    }

    //---------------------------------------------------------
    // Capture process output if desired.
    // Run a check for error by looking at error output stream.
    // This will block until process is done.
    // Store results in msg and return as strings.
    //---------------------------------------------------------
    //String[] stringsOut = gatherAllOutput(process, responseMsg, info.monitor);
    gatherAllOutput(1, responseMsg, info->monitor);

    //---------------------------------------------------------
    // if process was stopped/killed, include that in return message
    //---------------------------------------------------------
    if (info->killed) {
        err = cMsgAddInt32(responseMsg, "killed", 1);
        if (err != CMSG_OK) {
            printf("Cannot allocate memory 7\n");
            exit(-1);
        }
    }
    else if (info->stopped) {
        err = cMsgAddInt32(responseMsg, "stopped", 1);
        if (err != CMSG_OK) {
            printf("Cannot allocate memory 8\n");
            exit(-1);
        }
    }

    // remove the process from map since it's now terminated
    processMapRemove(idTable, id);
    
    //----------------------------------------------------------------
    // Respond to initial sendAndGet if we haven't done so already
    // so "startProcess" can return if it has not timed out already
    // (in which case it is not interested in this result anymore).
    //----------------------------------------------------------------
    if (info->wait) {
printf("SENDING MSG TO CMDR FOR WAITER ......\n");
        cMsgSend(domainId, responseMsg);
        return;
    }

    //----------------------------------------------------------------
    // If we're here it's because the Commander is not synchronously
    // waiting, but has registered a callback that the next message
    // will trigger.
    //
    // Now send another (regular) msg back to Commander to notify it
    // that the process is done and run any callback associated with
    // this process. As part of that, the original CommandReturn object
    // will be updated with the following information and given as an
    // argument to the callback.
    //----------------------------------------------------------------
    imDoneMsg = cMsgCreateMessage();
    if (imDoneMsg == NULL) {
        printf("Cannot allocate memory 9\n");
        exit(-1);
    }
    
    cMsgSetSubject(imDoneMsg, info->commander);
    cMsgSetType(imDoneMsg, remoteExecSubjectType);
        
    err = cMsgAddString(imDoneMsg, "returnType", "process_end");
    if (err != CMSG_OK) {
        printf("Cannot allocate memory 10\n");
        exit(-1);
    }

    err = cMsgAddInt32(imDoneMsg, "id", info->commandId);
    if (err != CMSG_OK) {
        printf("Cannot allocate memory 11\n");
        exit(-1);
    }

    if (stringsOut[0] != NULL && info->monitor) {
        err = cMsgAddString(imDoneMsg, "output", "output_here");
        if (err != CMSG_OK) {
            printf("Cannot allocate memory 12\n");
            exit(-1);
        }
    }

    if (stringsOut[1] != NULL) {
        err = cMsgAddString(imDoneMsg, "error", "error_here");
        if (err != CMSG_OK) {
            printf("Cannot allocate memory 13\n");
            exit(-1);
        }
    }

    if (info->killed) {
        err = cMsgAddInt32(imDoneMsg, "killed", 1);
        if (err != CMSG_OK) {
            printf("Cannot allocate memory 14\n");
            exit(-1);
        }
    }
    else if (info->stopped) {
        err = cMsgAddInt32(imDoneMsg, "stopped", 1);
        if (err != CMSG_OK) {
            printf("Cannot allocate memory 15\n");
            exit(-1);
        }
    }

printf("SENDING MSG TO RUN CALLBACK FOR PROCESS ......\n");
    cMsgSend(domainId, imDoneMsg);

    
    cMsgFreeMessage(&responseMsg);
    cMsgFreeMessage(&imDoneMsg);
    free(info);
    free(args);
}



/******************************************************************/
