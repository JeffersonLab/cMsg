/*---------------------------------------------------------------------------*
*  Copyright (c) 2010        Jefferson Science Associates,                   *
*                            Thomas Jefferson National Accelerator Facility  *
*                                                                            *
*    This software was developed under a United States Government license    *
*    described in the NOTICE file included as part of this distribution.     *
*                                                                            *
*    C.Timmer, 22-Nov-2010, Jefferson Lab                                    *
*                                                                            *
*    Authors: Carl Timmer                                                    *
*             timmer@jlab.org                   Jefferson Lab, #10           *
*             Phone: (757) 269-5130             12000 Jefferson Ave.         *
*             Fax:   (757) 269-6248             Newport News, VA 23606       *
*                                                                            *
*----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.remoteExec;

import org.jlab.coda.cMsg.*;

import java.io.IOException;
import java.io.InputStreamReader;
import java.io.BufferedReader;
import java.io.InputStream;
import java.util.Hashtable;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.net.InetAddress;
import java.net.UnknownHostException;
import java.lang.reflect.Constructor;

/**
 * This class, when used appropriately, can execute any command by
 * being sent the proper cMsg message by a Commander.
 *
 * @author timmer
 * Date: Oct 7, 2010
 */
public class Executor {

    /** Password needed to be sent by Commander for any process or thread to be run here. */
    private String password;

    /**
     * Used to generate unique id numbers in a thread-safe manner
     * which are used to identify a startProcess or startThread action.
     * This id is used to stop started processes and threads.
     */
    private AtomicInteger uniqueId = new AtomicInteger(1);

    /** UDL for connecting to cMsg server. */
    private String udl;
    /** Client name for connecting to cMsg server. */
    private String name;
    /** Connection to cMsg server. */
    private cMsg cmsgConnection;

    /** Map of running processes indexed by unique id. */
    private Hashtable<Integer, CommandInfo> processMap = new Hashtable<Integer, CommandInfo>(100);
    /** Map of running threads indexed by unique id. */
    private Hashtable<Integer, CommandInfo>  threadMap = new Hashtable<Integer, CommandInfo>(100);

    /**
     * Message containing info about this executor to be
     * sent to Commanders on start up and when requested.
     */
    private cMsgMessage statusMsg;

    /** Convenient data holding class for incoming commands. */
    private class CommandInfo {
        IExecutorThread execThread;
        Process process;
        String  className;
        String  command;
        String  commander;
        int     commandId;
        boolean monitor;
        boolean wait;
        boolean isProcess;
        AtomicBoolean killed = new AtomicBoolean();
        AtomicBoolean stopped = new AtomicBoolean();
        cMsgMessage argsMessage;
    }


    /**
     * Constructor. If name arg is null, local hostname becomes our cmsg client name.
     *
     * @param password password needed to be sent by Commander for any process or thread to be run here.
     *                 Password may be null if not used, but must not exceed 16 characters in length
     *                 otherwise.
     * @param udl UDL for connecting to cMsg server.
     * @param name client name for connecting to cMsg server.
     * @throws cMsgException if cannot find our hostname or host platform information;
     *                       udl arg is null; password != null &amp;&amp; &gt; 16 characters
     */
    public Executor(String password, String udl, String name) throws cMsgException {

        if (udl == null) {
            throw new cMsgException("Need to specify a udl");
        }

        if (password != null && password.length() > 16) {
            throw new cMsgException("Password must be <= 16 characters");
        }

        this.udl = udl;
        this.password = password;

        // Find out who we are.
        String host;
        try {
            host = InetAddress.getLocalHost().getCanonicalHostName();
        } catch (UnknownHostException e) {
            throw new cMsgException("Cannot find our own hostname", e);
        }

        // By default give ourselves the host's name.
        if (name != null) {
            this.name = name;
        }
        else {
            this.name = host;
        }

        // Run some uname commands to find out system information.
        try {
            Process pr = Runtime.getRuntime().exec("uname -s");
            BufferedReader br = new BufferedReader(new InputStreamReader(pr.getInputStream()));
            String os = br.readLine();

            pr = Runtime.getRuntime().exec("uname -m");
            br = new BufferedReader(new InputStreamReader(pr.getInputStream()));
            String machine = br.readLine();

            pr = Runtime.getRuntime().exec("uname -p");
            br = new BufferedReader(new InputStreamReader(pr.getInputStream()));
            String processor = br.readLine();

            pr = Runtime.getRuntime().exec("uname -r");
            br = new BufferedReader(new InputStreamReader(pr.getInputStream()));
            String release = br.readLine();

            try {
                // Create a status message sent out to all commanders as soon
                // as we connect to the cMsg server and to all commanders who
                // specifically ask for it.
                statusMsg = new cMsgMessage();
                statusMsg.setHistoryLengthMax(0);
                // This subject of this msg gets changed depending on who it's sent to.
                statusMsg.setSubject(Commander.allSubjectType);
                statusMsg.setType(Commander.remoteExecSubjectType);

                cMsgPayloadItem item0 = new cMsgPayloadItem("returnType", InfoType.REPORTING.getValue());
                statusMsg.addPayloadItem(item0);
                cMsgPayloadItem item1 = new cMsgPayloadItem("name", this.name);
                statusMsg.addPayloadItem(item1);
                cMsgPayloadItem item2 = new cMsgPayloadItem("os", os);
                statusMsg.addPayloadItem(item2);
                cMsgPayloadItem item3 = new cMsgPayloadItem("machine", machine);
                statusMsg.addPayloadItem(item3);
                cMsgPayloadItem item4 = new cMsgPayloadItem("processor", processor);
                statusMsg.addPayloadItem(item4);
                cMsgPayloadItem item5 = new cMsgPayloadItem("release", release);
                statusMsg.addPayloadItem(item5);
                cMsgPayloadItem item6 = new cMsgPayloadItem("host", host);
                statusMsg.addPayloadItem(item6);
            }
            catch (cMsgException e) {/* never happen */}

        } catch (IOException e) {
            // return error message if execution failed
            throw new cMsgException(e);
        }

        // start thread to maintain connection with cmsg server
        new CmsgConnectionHandler();
    }


    /**
     * This class attempts to keep this cmsg client connected to a server
     * by checking the connection every second and reconnecting if necessary.
     */
    private class CmsgConnectionHandler extends Thread {

        CmsgConnectionHandler() {
            setDaemon(true);
            start();
        }

        public void run() {

            // make connection to server or keep trying to connect
            while (true) {

                try { Thread.sleep(1000); }
                catch (InterruptedException e) { return; }

                if (cmsgConnection == null || !cmsgConnection.isConnected()) {
//System.out.println("Try to reconnect");
                    try {
                        // connect
                        cmsgConnection = new cMsg(udl, name, "cmsg java executor");
                        cmsgConnection.connect();
                        // add subscriptions
                        CommandCallback cb = new CommandCallback();
                        cmsgConnection.subscribe(Commander.remoteExecSubjectType, name,  cb, null);
                        cmsgConnection.subscribe(Commander.remoteExecSubjectType, Commander.allSubjectType, cb, null);
                        cmsgConnection.start();

                        // Send out a general message telling all commanders
                        // that there is a new executor running.
                        sendStatusTo(Commander.allSubjectType);
                    }
                    catch (cMsgException e) {
                        //e.printStackTrace();
                    }
                }
            }
        }
    }


    /**
     * This class defines the callback to be run when a message
     * containing a valid command arrives.
     */
    private class CommandCallback extends cMsgCallbackAdapter {

        public void callback(cMsgMessage msg, Object userObject) {

            // there must be payload
            if (msg.hasPayload()) {
                cMsgPayloadItem item;
                String passwd = null;
                String commandType;

                try {
                    // read password
                    item = msg.getPayloadItem("p");
                    if (item != null) {
                        // decrypt password here
                        passwd = ExecutorSecurity.decrypt(item.getString());
                    }

                    // check password if required
                    if (password != null && !password.equals(passwd)) {
                        System.out.println("Reject message, wrong password");
                        return;
                    }

                    // What command are we given?
                    item = msg.getPayloadItem("commandType");
                    if (item == null) {
                        System.out.println("Reject message, no command type");
                        return;
                    }
                    commandType = item.getString();
//System.out.println("commandtype = " + commandType);
                    CommandType type = CommandType.getCommandType(commandType);
                    if (type == null) {
                        System.out.println("Reject message, command type not recognized");
                        return;
                    }

                    switch(type) {
                        case START_PROCESS:
                            if (!msg.isGetRequest()) {
                                // error
                                System.out.println("Reject message, start_process cmd must be sendAndGet msg");
                                return;
                            }

                            // Store incoming data here
                            CommandInfo commandInfo = new CommandInfo();
                            commandInfo.isProcess = true;

                            item = msg.getPayloadItem("command");
                            if (item == null) {
                                System.out.println("Reject message, no command");
                                return;
                            }
                            // decrypt command here
                            commandInfo.command = ExecutorSecurity.decrypt(item.getString());

                            item = msg.getPayloadItem("monitor");
                            boolean monitor = false;
                            if (item != null) {
                                monitor = item.getInt() != 0;
                            }
                            commandInfo.monitor = monitor;

                            item = msg.getPayloadItem("wait");
                            boolean wait = false;
                            if (item != null) {
                                wait = item.getInt() != 0;
                            }
                            commandInfo.wait = wait;

                            item = msg.getPayloadItem("commander");
                            if (item == null) {
                                System.out.println("Reject message, no commander");
                                return;
                            }
                            commandInfo.commander = item.getString();

                            item = msg.getPayloadItem("id");
                            if (item == null) {
                                System.out.println("Reject message, no commander id");
                                return;
                            }
                            commandInfo.commandId = item.getInt();

                            // return must be placed in sendAndGet response msg
                            ProcessRun pr = new ProcessRun(commandInfo, msg.response());
                            pr.start();

                            break;

                        case START_THREAD:
                            if (!msg.isGetRequest()) {
                                // error
                                System.out.println("Reject message, start_thread cmd must be sendAndGet msg");
                                return;
                            }

                            commandInfo = new CommandInfo();
                            // don't send back any output since it cannot be isolated to any 1 thread
                            commandInfo.monitor = false;
                            commandInfo.isProcess = false;

                            item = msg.getPayloadItem("className");
                            if (item == null || item.getString() == null) {
                                System.out.println("Reject message, no class name");
                                return;
                            }
                            // decrypt class name here
                            commandInfo.className = ExecutorSecurity.decrypt(item.getString());

                            item = msg.getPayloadItem("wait");
                            wait = false;
                            if (item != null) {
                                wait = item.getInt() != 0;
                            }
                            commandInfo.wait = wait;

                            item = msg.getPayloadItem("commander");
                            if (item == null) {
                                System.out.println("Reject message, no commander");
                                return;
                            }
                            commandInfo.commander = item.getString();

                            item = msg.getPayloadItem("id");
                            if (item == null) {
                                System.out.println("Reject message, no commander id");
                                return;
                            }
                            commandInfo.commandId = item.getInt();

                            // optional args to constructor contained msg payload
                            item = msg.getPayloadItem("args");
                            if (item != null) {
                                commandInfo.argsMessage = item.getMessage();
                            }

                            // return must be placed in sendAndGet response msg
                            ThreadRun tr = new ThreadRun(commandInfo, msg.response());
                            tr.start();

                            break;

                        case STOP_ALL:
                            stopAll(false);
                            break;

                        case STOP:
                            item = msg.getPayloadItem("id");
                            if (item == null) {
                                System.out.println("Reject message, no id");
                                return;
                            }
                            stop(item.getInt());
                            break;

                        case DIE:
                            item = msg.getPayloadItem("killProcesses");
                            boolean killProcesses = false;
                            if (item != null) {
                                killProcesses = item.getInt() != 0;
                            }
                            if (killProcesses) stopAll(true);
                            System.exit(0);
                            break;

                        case IDENTIFY:
                            item = msg.getPayloadItem("commander");
                            if (item == null) {
                                System.out.println("Reject message, no commander");
                                return;
                            }
                            String commander = item.getString();
                            sendStatusTo(commander);
                            break;

                        default:
                    }
                }
                catch (cMsgException e) {
                    System.out.println("Reject message, bad format");
                }
            }
            else {
                System.out.println("Reject message, no payload");
            }
        }
    }


    /**
     * Send a status message to a specified Commander.
     * @param commander Commander to send status message to.
     * @throws cMsgException if cmsg connection broken
     */
    private void sendStatusTo(String commander) throws cMsgException {
        // This msg only goes to the Commander specified in the arg.
        // That may be ".all" which goes to all commanders.
        statusMsg.setSubject(commander);
        cmsgConnection.send(statusMsg);
    }


    /**
     * Stop all processes and threads currently running.
     * @param kill have we been commanded to kill executor?
     */
    private void stopAll(boolean kill) {
        // stop processes
        synchronized(processMap) {
            for (CommandInfo info : processMap.values()) {
                if (kill) {
                    info.killed.set(true);
                }
                else {
                    info.stopped.set(true);
                }
                info.process.destroy();
            }
        }

        // stop threads (doesn't matter they're alive or not)
        synchronized(threadMap) {
            for (CommandInfo info : threadMap.values()) {
                if (kill) {
                    info.killed.set(true);
                }
                else {
                    info.stopped.set(true);
                }
                info.execThread.shutItDown();
            }
        }
    }


    /**
     * Stop the specified process or thread currently running.
     * @param id id number specified process or thread currently running.
     */
    private void stop(int id) {
        // We don't have to remove this process/thread from the hashtable
        // because the ProcessRun or ThreadRun class will do it after it
        // is interrupted.
        CommandInfo info = processMap.get(id);
        if (info == null) {
            info = threadMap.get(id);
        }

        if (info == null) {
            // process/thread already stopped
            return;
        }

        // stop process
        info.stopped.set(true);
        if (info.isProcess) {
            info.process.destroy();
        }
        else {
            // stop thread (doesn't matter if alive or not)
            info.execThread.shutItDown();
        }

    }


    /**
     * Get the output of the process - either error or regular output
     * depending on the input stream.
     *
     * @param inputStream get process output from this stream
     * @return String of process output
     */
    private String getProcessOutput(InputStream inputStream) {
        String line;
        StringBuilder sb = new StringBuilder(300);
        BufferedReader brErr = new BufferedReader(new InputStreamReader(inputStream));

        try {
            // read each line of output
            while ((line = brErr.readLine()) != null) {
                sb.append(line);
                sb.append("\n");
            }
        }
        catch (IOException e) {
            // probably best to ignore this error
System.out.println("startProcess: io error gathering (error) output");
        }

        if (sb.length() > 0) {
            // take off last \n we put in buffer
            sb.deleteCharAt(sb.length()-1);
            return sb.toString();
        }

        return null;
    }


    /**
     * Get both regular output (if monitor is true) and error output,
     * store it in a cMsg message, and return both as strings.
     *
     * @param msg cMsg message to store output in.
     * @param monitor <code>true</code> if we store regular output, else <code>false</code>.
     * @return array with both regular output (first element) and error output (second).
     */
    private String[] gatherAllOutput(Process process, cMsgMessage msg, boolean monitor) {
        String output;
        String[] strs = new String[2];

        // Grab regular output if requested.
        if (monitor) {
            output = getProcessOutput(process.getInputStream());
            if (output != null) {
                strs[0] = output;
                try {
                    cMsgPayloadItem item = new cMsgPayloadItem("output", output);
                    msg.addPayloadItem(item);
                }
                catch (cMsgException e) {/* never happen */}
            }
        }

        // Always grab error output.
        output = getProcessOutput(process.getErrorStream());
        if (output != null) {
            strs[1] = output;
            try {
                cMsgPayloadItem item = new cMsgPayloadItem("error", output);
                msg.addPayloadItem(item);
                cMsgPayloadItem item1 = new cMsgPayloadItem("immediateError", 0);
                msg.addPayloadItem(item1);
            }
            catch (cMsgException e) {/* never happen */}
        }

        return strs;
    }


    /**
     * Class for running a command in its own thread since any command could
     * easily block for an extended time. This way the callback doesn't get
     * tied up running one command.
     */
    private class ProcessRun extends Thread {

        private CommandInfo info;
        private cMsgMessage responseMsg;

        ProcessRun(CommandInfo info, cMsgMessage responseMsg) {
            this.info = info;
            this.responseMsg = responseMsg;
        }

        public void run() {
            startProcess();
        }


        /**
         * Reading from the error or output streams will automatically block.
         * Therefore, if the caller does NOT want to wait, it cannot monitor
         * either (except AFTER it has returned the sendAndGet response).
         */
        private void startProcess() {
            //----------------------------------------------------------------
            // run the command
            //----------------------------------------------------------------
            Process process;
            try {
//System.out.println("run -> " + info.command);
                process = Runtime.getRuntime().exec(info.command);
                // Allow process a chance to run before testing if its terminated.
                Thread.yield();
                try {Thread.sleep(100);}
                catch (InterruptedException e) {}
            }
            catch (Exception e) {
                // return error message if execution failed
                try {
                    cMsgPayloadItem item1 = new cMsgPayloadItem("terminated", 1);
                    responseMsg.addPayloadItem(item1);
                    cMsgPayloadItem item2 = new cMsgPayloadItem("error", e.getMessage());
                    responseMsg.addPayloadItem(item2);
                    cMsgPayloadItem item3 = new cMsgPayloadItem("immediateError", 1);
                    responseMsg.addPayloadItem(item3);
                    cmsgConnection.send(responseMsg);
                }
                catch (cMsgException e1) {
                    // sending msg failed due to broken connection to server, all bets off
                }
                return;
            }

            //---------------------------------------------------------
            // Figure out if process has already terminated.
            //---------------------------------------------------------
            boolean terminated = true;
            try { process.exitValue(); }
            catch (Exception e) { terminated = false;  }

            //----------------------------------------------------------------
            // If process is NOT terminated, then put this process in
            // hash table storage so it can be terminated later.
            //----------------------------------------------------------------
            int id = 0;
            if (!terminated) {
                id = uniqueId.incrementAndGet();
                try {
                    cMsgPayloadItem item = new cMsgPayloadItem("id", id);
                    responseMsg.addPayloadItem(item);
                }
                catch (cMsgException e) {/* never happen */}
                info.process = process;
                processMap.put(id, info);
            }

            //----------------------------------------------------------------
            // If commander NOT waiting for process to finish,
            // send return message at once.
            //----------------------------------------------------------------
            if (!info.wait) {
                try {
                    // Grab any output available if process already terminated.
                    if (terminated) {
                        cMsgPayloadItem item1 = new cMsgPayloadItem("terminated", 1);
                        responseMsg.addPayloadItem(item1);
                        // grab any output and put in response message
                        gatherAllOutput(process, responseMsg, info.monitor);
                    }

                    // Send response to Commander's sendAndGet.
                    cmsgConnection.send(responseMsg);
                    // Now, finish waiting for process and notify Commander when finished.
                }
                catch (cMsgException e) {
                    // sending msg failed due to broken connection to server, all bets off
                }
            }

            //---------------------------------------------------------
            // If we're here, we want to wait until process is finished.
            //---------------------------------------------------------
            try {
                cMsgPayloadItem item = new cMsgPayloadItem("terminated", 1);
                responseMsg.addPayloadItem(item);
            }
            catch (cMsgException e) {/* never happen */}

            //---------------------------------------------------------
            // Capture process output if desired.
            // Run a check for error by looking at error output stream.
            // This will block until process is done.
            // Store results in msg and return as strings.
            //---------------------------------------------------------
            String[] stringsOut = gatherAllOutput(process, responseMsg, info.monitor);

            //---------------------------------------------------------
            // if process was stopped/killed, include that in return message
            //---------------------------------------------------------
            if (info.killed.get()) {
                try {
                    cMsgPayloadItem item = new cMsgPayloadItem("killed", 1);
                    responseMsg.addPayloadItem(item);
                }
                catch (cMsgException e) {/* never happen */}
            }
            else if (info.stopped.get()) {
                try {
                    cMsgPayloadItem item = new cMsgPayloadItem("stopped", 1);
                    responseMsg.addPayloadItem(item);
                }
                catch (cMsgException e) {/* never happen */}
            }

            // remove the process from map since it's now terminated
            processMap.remove(id);

            try {
                //----------------------------------------------------------------
                // Respond to initial sendAndGet if we haven't done so already
                // so "startProcess" can return if it has not timed out already
                // (in which case it is not interested in this result anymore).
                //----------------------------------------------------------------
                if (info.wait) {
//System.out.println("Sending msg to waiting Commander ......");
                    cmsgConnection.send(responseMsg);
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
                cMsgMessage imDoneMsg = new cMsgMessage();
                imDoneMsg.setSubject(info.commander);
                imDoneMsg.setType(Commander.remoteExecSubjectType);
                cMsgPayloadItem item1 = new cMsgPayloadItem("returnType", InfoType.PROCESS_END.getValue());
                imDoneMsg.addPayloadItem(item1);
                cMsgPayloadItem item2 = new cMsgPayloadItem("id", info.commandId);
                imDoneMsg.addPayloadItem(item2);

                if (stringsOut[0] != null && info.monitor) {
                    cMsgPayloadItem item = new cMsgPayloadItem("output", stringsOut[0]);
                    imDoneMsg.addPayloadItem(item);
                }

                if (stringsOut[1] != null) {
                    cMsgPayloadItem item = new cMsgPayloadItem("error", stringsOut[1]);
                    imDoneMsg.addPayloadItem(item);
                }

                if (info.killed.get()) {
                    try {
                        cMsgPayloadItem item = new cMsgPayloadItem("killed", 1);
                        imDoneMsg.addPayloadItem(item);
                    }
                    catch (cMsgException e) {/* never happen */}
                }
                else if (info.stopped.get()) {
                    try {
                        cMsgPayloadItem item = new cMsgPayloadItem("stopped", 1);
                        imDoneMsg.addPayloadItem(item);
                    }
                    catch (cMsgException e) {/* never happen */}
                }

//System.out.println("Sending msg to run callback for Commander ......");
                cmsgConnection.send(imDoneMsg);
            }
            catch (cMsgException e) {
                // sending msg failed due to broken connection to server, all bets off
            }
        }
    }



    /**
     * Class for running a command in its own thread since any command could
     * easily block for an extended time. This way the callback doesn't get
     * tied up running one command.
     */
    private class ThreadRun extends Thread {
        private int id;
        private CommandInfo info;
        private cMsgMessage responseMsg;

        ThreadRun(CommandInfo info, cMsgMessage responseMsg) {
            this.info = info;
            this.responseMsg = responseMsg;
        }

        public void run() {
            startThread();
        }

        /**
         * Recursive method that creates an object of type className from the cMsg message.
         * It gets a list of constructor args from the message and the means to create
         * each of those args.
         *
         * @param msg cMsg message containing all data necessary to create object.
         * @param className name of the class of the created object.
         * @return created object.
         * @throws cMsgException if error in creating object.
         */
        private Object createObjectFromMessage(cMsgMessage msg, String className) throws cMsgException {

            //----------------------------------------------------------------
            // create an object from the given class name
            //----------------------------------------------------------------
            Object returnObject;

            try {
                Class c = Class.forName(className);
                Constructor conn = c.getConstructor();

                // if no args, use no-arg constructor
                if (msg == null) {
                    returnObject = conn.newInstance();
                }
                else {
                    // number of constructor arguments contained in userInt
                    int numArgs = msg.getUserInt();

                    // types of parameters defining constructor (primitives != reference types !!!)
                    Class[] parameterTypes = new Class[numArgs];

                    // classes of the constructor arguments (primitives use corresponding wrappers)
                    Class[] argClasses = new Class[numArgs];

                    // constructor arguments
                    Object[] args = new Object[numArgs];

                    // class of each constructor argument
                    cMsgPayloadItem classesItem = msg.getPayloadItem("classes");
                    // type of each arg and its constructor (primitive, reference, null, etc)
                    cMsgPayloadItem argsTypesItem = msg.getPayloadItem("argTypes");
                    if (classesItem == null || argsTypesItem == null) {
                        throw new cMsgException("Protocol violation: classes and/or types arrays null");
                    }
                    String[] classNames = classesItem.getStringArray();
                    int[] argTypeValues = argsTypesItem.getIntArray();
                    if (classNames.length < numArgs || argTypeValues.length < numArgs) {
                        throw new cMsgException("Protocol violation: too few classes and/or types");
                    }

                    // Transform the incoming information about the type and
                    // constructor of each argument (NOT returnObject),
                    // and turn it into arrays containing the parameter types of the
                    // argument constructors and the classes of arguments being
                    // constructed.
                    ArgType aType;
                    ArgType argTypes[] = new ArgType[numArgs];
                    int numStringArgs = 0, numMessageArgs = 0;

                    for (int i=0; i < numArgs; i++) {
                        aType = ArgType.getArgType(argTypeValues[i]);
                        if (aType == null) {
                            throw new cMsgException("Protocol violation: bad arg type value");
                        }

                        // Count # of primitive type args being constructed
                        if (aType == ArgType.PRIMITIVE) {
                            numStringArgs++;
                        }
                        // Count # of reference types being constructed (with args of their own)
                        else if (aType == ArgType.REFERENCE) {
                            numMessageArgs++;
                        }
                        argTypes[i] = aType;

                        // Be sure to differentiate between primitive ...
                        if (classNames[i].equals("int")) {
                            parameterTypes[i] = int.class;
                            argClasses[i] = java.lang.Integer.class;
                        }
                        else if (classNames[i].equals("boolean")) {
                            parameterTypes[i] = boolean.class;
                            argClasses[i] = java.lang.Boolean.class;
                        }
                        else if (classNames[i].equals("byte")) {
                            parameterTypes[i] = byte.class;
                            argClasses[i] = java.lang.Byte.class;
                        }
                        else if (classNames[i].equals("short")) {
                            parameterTypes[i] = short.class;
                            argClasses[i] = java.lang.Short.class;
                        }
                        else if (classNames[i].equals("long")) {
                            parameterTypes[i] = long.class;
                            argClasses[i] = java.lang.Long.class;
                        }
                        else if (classNames[i].equals("float")) {
                            parameterTypes[i] = float.class;
                            argClasses[i] = java.lang.Float.class;
                        }
                        else if (classNames[i].equals("double")) {
                            parameterTypes[i] = double.class;
                            argClasses[i] = java.lang.Double.class;
                        }
                        else if (classNames[i].equals("char")) {
                            parameterTypes[i] = char.class;
                            argClasses[i] = java.lang.Character.class;
                        }
                        // and reference types.
                        else {
                            parameterTypes[i] = Class.forName(classNames[i]); // throws ClassNotFoundException
                            argClasses[i] = parameterTypes[i];
                        }
//System.out.println("arg #" + i + " -> type = " + aType.name() + ", class = " + classNames[i]);
                    }

                    // Now get the values of each arguments' constructor arguments if any.
                    // Constructors for primitive types take a single String as an arg.
                    // Constructors for reference types (whose constructors take args),
                    // take a cMsg message as an arg. The msg, of course, must be decoded first,
                    // which is exactly what this method does.
                    cMsgPayloadItem stringValuesItem  = msg.getPayloadItem("stringArgs");
                    cMsgPayloadItem messageValuesItem = msg.getPayloadItem("messageArgs");
                    if (numStringArgs > 0 &&
                            (stringValuesItem == null || stringValuesItem.getCount() < numStringArgs)) {
                        throw new cMsgException("Protocol violation: string and/or message value arrays null");
                    }
                    if (numMessageArgs > 0 &&
                            (messageValuesItem == null || messageValuesItem.getCount() < numMessageArgs)) {
                        throw new cMsgException("Protocol violation: too few string and/or message values");
                    }

                    String stringArgs[] = null;
                    cMsgMessage msgArgs[] = null;
                    if (numStringArgs > 0) stringArgs = stringValuesItem.getStringArray();
                    if (numMessageArgs > 0)   msgArgs = messageValuesItem.getMessageArray();

                    int stringArgIndex  = 0;
                    int messageArgIndex = 0;

                    // To instantiate a primitive type's wrapper object,
                    // get its constructor with a String arg. This works
                    // for all primitive types except char. The only
                    // constructor for a Character is C(char value).
                    Class[] stringParam = {String.class};
                    Class[] charParam   = {Character.class};
                    Object[] singleArg  = new Object[1];

                    // What we do depends on what type of argument we're constructing ...
                    for (int i=0; i < numArgs; i++) {
                        switch (argTypes[i]) {
                            case PRIMITIVE:
//System.out.println("Create object of " + classNames[i] + " with arg " + stringArgs[stringArgIndex]);
                                // Get the proper constructor.
                                Constructor con = argClasses[i].getConstructor(stringParam);
                                // Get the constructor's argument.
                                singleArg[0] = stringArgs[stringArgIndex++];
                                // Use this constructor to create object which
                                // is argument of returnObject constructor.
                                args[i] = con.newInstance(singleArg);
                                break;

                            case PRIMITIVE_CHAR:
//System.out.println("Create object of " + classNames[i] + " with arg " + stringArgs[stringArgIndex]);
                                con = argClasses[i].getConstructor(charParam);
                                // change String to int to char to Character to Object (whew!)
                                singleArg[0] = (char) (Integer.parseInt(stringArgs[stringArgIndex++]));
                                args[i] = con.newInstance(singleArg);
                                break;

                            case REFERENCE:
//System.out.println("Create object of " + classNames[i] + " from msg ");
                                // recursive stuff here
                                cMsgMessage argMsg = msgArgs[messageArgIndex++];
                                Object argObject = createObjectFromMessage(argMsg, classNames[i]);
                                args[i] = argObject;
                                break;

                            case REFERENCE_NOARG:
//System.out.println("Use no-arg constructor of " + classNames[i]);
                                args[i] = argClasses[i].getConstructor().newInstance();
                                break;

                            case NULL:
//System.out.println("Use null for arg #" + (i+1));
                                args[i] = null;
                                break;

                            default:
                                // never get here
                        }
                    }

                    // get the proper constructor
                    Constructor co = c.getConstructor(parameterTypes);

                    // create an instance and return it
                    returnObject =  co.newInstance(args);
                }
            }
            catch (Exception e) {
                e.printStackTrace();
                throw new cMsgException(e);
            }

            return returnObject;
        }


        /**
         * Start the thread and wait for it if necessary so it can notify Commander that it's done.
         */
        private void startThread() {
            //----------------------------------------------------------------
            // create an object of the given class name
            //----------------------------------------------------------------
            IExecutorThread eThread;
            try {
                eThread = (IExecutorThread)createObjectFromMessage(info.argsMessage, info.className);
            }
            catch (cMsgException e) {
                e.printStackTrace();
                // return error if object creation failed
                try {
                    cMsgPayloadItem item1 = new cMsgPayloadItem("terminated", 1);
                    responseMsg.addPayloadItem(item1);
                    cMsgPayloadItem item2 = new cMsgPayloadItem("error", e.getMessage());
                    responseMsg.addPayloadItem(item2);
                    cMsgPayloadItem item3 = new cMsgPayloadItem("immediateError", 1);
                    responseMsg.addPayloadItem(item3);
                    cmsgConnection.send(responseMsg);
                }
                catch (cMsgException e1) {
                    // sending msg failed due to broken connection to server, all bets off
                }
                return;
            }

            //----------------------------------------------------------------
            // Start up our thread.
            //----------------------------------------------------------------
            eThread.startItUp();

            //----------------------------------------------------------------
            // Store ID so we can terminate thread/executor later
            //----------------------------------------------------------------
            int id = uniqueId.incrementAndGet();
            try {
                cMsgPayloadItem item = new cMsgPayloadItem("id", id);
                responseMsg.addPayloadItem(item);
            }
            catch (cMsgException e) {/* never happen */}
            info.execThread = eThread;
            threadMap.put(id, info);

            //----------------------------------------------------------------
            // If commander NOT waiting for process to finish,
            // send return message at once.
            //----------------------------------------------------------------
            if (!info.wait) {
                try {
                    cmsgConnection.send(responseMsg);
                }
                catch (cMsgException e) {
                    // sending msg failed due to broken connection to server, all bets off
                }
            }

            //----------------------------------------------------------------
            // Wait for thread to finish.
            //----------------------------------------------------------------
            try {
                eThread.waitUntilDone();
            }
            catch (InterruptedException e) {
                // Executor interrupted, all bets off
            }

            //---------------------------------------------------------
            // If we're here, we waited until thread was fnished.
            //---------------------------------------------------------
            try {
                cMsgPayloadItem item = new cMsgPayloadItem("terminated", 1);
                responseMsg.addPayloadItem(item);
            }
            catch (cMsgException e) {/* never happen */}

            //---------------------------------------------------------
            // if thread was stopped/killed, include that in return message
            //---------------------------------------------------------
            if (info.killed.get()) {
                try {
                    cMsgPayloadItem item = new cMsgPayloadItem("killed", 1);
                    responseMsg.addPayloadItem(item);
                }
                catch (cMsgException e) {/* never happen */}
            }
            else if (info.stopped.get()) {
                try {
                    cMsgPayloadItem item = new cMsgPayloadItem("stopped", 1);
                    responseMsg.addPayloadItem(item);
                }
                catch (cMsgException e) {/* never happen */}
            }

            // send msg back to Commander
            try {
                //----------------------------------------------------------------
                // Respond to initial sendAndGet if we haven't done so already
                // so "startThread" can return if it has not timed out already
                // (in which case it is not interested in this result anymore).
                //----------------------------------------------------------------
                if (info.wait) {
                    cmsgConnection.send(responseMsg);
                    return;
                }

                // remove the thread from map since it's now terminated 
                threadMap.remove(id);

                //----------------------------------------------------------------
                // If we're here it's because the Commander is not synchronously
                // waiting, but has registered a callback that the next message
                // will trigger.
                //
                // Now send another (regular) msg back to Commander to notify it
                // that the thread is done and run any callback associated with
                // the thread.
                //----------------------------------------------------------------
                cMsgMessage imDoneMsg = new cMsgMessage();
                imDoneMsg.setSubject(info.commander);
                imDoneMsg.setType(Commander.remoteExecSubjectType);
                cMsgPayloadItem item1 = new cMsgPayloadItem("returnType", InfoType.THREAD_END.getValue());
                imDoneMsg.addPayloadItem(item1);
                cMsgPayloadItem item2 = new cMsgPayloadItem("id", info.commandId);
                imDoneMsg.addPayloadItem(item2);
                if (info.killed.get()) {
                    try {
                        cMsgPayloadItem item = new cMsgPayloadItem("killed", 1);
                        imDoneMsg.addPayloadItem(item);
                    }
                    catch (cMsgException e) {/* never happen */}
                }
                else if (info.stopped.get()) {
                    try {
                        cMsgPayloadItem item = new cMsgPayloadItem("stopped", 1);
                        imDoneMsg.addPayloadItem(item);
                    }
                    catch (cMsgException e) {/* never happen */}
                }
                cmsgConnection.send(imDoneMsg);
            }
            catch (cMsgException e) {
                // sending msg failed due to broken connection to server, all bets off
            }
        }

    }




    /**
     * Method to decode the command line used to start this application.
     * @param args command line arguments
     */
    private static String[] decodeCommandLine(String[] args) {
        String[] stuff = new String[3];

        // loop over all args
        for (int i = 0; i < args.length; i++) {
            if (args[i].equalsIgnoreCase("-n")) {
                stuff[2] = args[i + 1];  // name
                i++;
            }
            else if (args[i].equalsIgnoreCase("-u")) {
                stuff[1]= args[i + 1];   // udl
                i++;
            }
            else if (args[i].equalsIgnoreCase("-p")) {
                stuff[0]= args[i + 1];   // password
                i++;
            }
        }

        return stuff;
    }

    /**
     * Run as a stand-alone application
     * @param args arguments.
     */
    public static void main(String[] args) {
        try {
            String[] arggs = decodeCommandLine(args);
            System.out.println("Starting Executor with:\n  password = " + arggs[0] +
                               "\n  name = " + arggs[2] + "\n  udl = " + arggs[1]);
            new Executor(arggs[0], arggs[1], arggs[2]);
            while(true) {
                try {
                    Thread.sleep(2000);
                }
                catch (InterruptedException e) {
                    return;
                }
            }
        }
        catch (cMsgException e) {
            e.printStackTrace();
            System.exit(-1);
        }
    }


}
