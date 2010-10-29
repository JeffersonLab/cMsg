package org.jlab.coda.cMsg.remoteExec;

import org.jlab.coda.cMsg.*;

import javax.crypto.SecretKey;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.BufferedReader;
import java.io.InputStream;
import java.util.HashMap;
import java.util.concurrent.atomic.AtomicInteger;
import java.net.InetAddress;
import java.net.UnknownHostException;
import java.lang.reflect.Constructor;

/**
 * This class, when used appropriately, can execute any command on any node by
 * being sent the proper cMsg message.
 *
 * @author timmer
 * Date: Oct 7, 2010
 */
public class Executor {

    private SecretKey key;

    private AtomicInteger uniqueId = new AtomicInteger(1);

    private String password;
    private String name;
    private String udl;

    private HashMap<Integer, CommandInfo> processMap = new HashMap<Integer, CommandInfo>(100);
    private HashMap<Integer, CommandInfo>  threadMap = new HashMap<Integer, CommandInfo>(100);
    private cMsg cmsgConnection;
    private cMsgMessage statusMsg;
    private volatile boolean connected;


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
        cMsgMessage argsMessage;
    }


    public Executor(String password, SecretKey key, String udl, String name)
            throws cMsgException {
        this.key = key;
        this.udl = udl;
        this.password = password;

        if (name != null) {
            this.name = name;
        }
        else {
            // Find out who we are. By default give ourselves the host's name.
            try {
                this.name = InetAddress.getLocalHost().getCanonicalHostName();
            } catch (UnknownHostException e) {
                throw new cMsgException("Cannot find our own hostname", e);
            }
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
            cMsgPayloadItem item1 = new cMsgPayloadItem("name", name);
            statusMsg.addPayloadItem(item1);
            cMsgPayloadItem item2 = new cMsgPayloadItem("os", os);
            statusMsg.addPayloadItem(item2);
            cMsgPayloadItem item3 = new cMsgPayloadItem("machine", machine);
            statusMsg.addPayloadItem(item3);
            cMsgPayloadItem item4 = new cMsgPayloadItem("processor", processor);
            statusMsg.addPayloadItem(item4);
            cMsgPayloadItem item5 = new cMsgPayloadItem("release", release);
            statusMsg.addPayloadItem(item5);

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
    class CmsgConnectionHandler extends Thread {

        CmsgConnectionHandler() {
            setDaemon(true);
            start();
        }

        public void run() {
            // make connection to server or keep trying
            while (true) {
                if (!connected) {
                    try {
System.out.println("Connect to server with name = " + name + ", udl = " + udl);
                        // connect
                        cmsgConnection = new cMsg(udl, name, "cmsg executor");
                        cmsgConnection.connect();
                        // add subscriptions
                        CommandCallback cb = new CommandCallback();
System.out.println("Subscribe to sub = " + Commander.remoteExecSubjectType + ", typ = " + name);
                        cmsgConnection.subscribe(Commander.remoteExecSubjectType, name,  cb, null);
System.out.println("Subscribe to sub = " + Commander.remoteExecSubjectType + ", typ = " + Commander.allSubjectType);
                        cmsgConnection.subscribe(Commander.remoteExecSubjectType, Commander.allSubjectType, cb, null);
                        cmsgConnection.start();
                        connected = true;

                        // Send out a general message telling all commanders
                        // that there is a new executor running.
                        sendStatusTo(Commander.allSubjectType);
                    }
                    catch (cMsgException e) {
                        e.printStackTrace();
                    }
                }

                try {
                    Thread.sleep(1000);
                }
                catch (InterruptedException e) {
                    return;
                }
            }
        }
    }


    private void sendStatusTo(String commander) throws cMsgException {
        // This msg only goes to the Commander specified in the arg.
        // That may be ".all" which goes to all commanders.
        statusMsg.setSubject(commander);
        cmsgConnection.send(statusMsg);
    }


    /**
     * This class defines the callback to be run when a message
     * containing a valid command arrives.
     */
    class CommandCallback extends cMsgCallbackAdapter {

        public void callback(cMsgMessage msg, Object userObject) {

            // there must be payload
            if (msg.hasPayload()) {
                cMsgPayloadItem item;
                String passwd = null;
                String commandType;

                try {
                    item = msg.getPayloadItem("password");
                    if (item != null) {
                        passwd = item.getString();
                        // decrypt password here
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
System.out.println("commandtype = " + commandType);
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
                            commandInfo.command = item.getString();

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
                            commandInfo.className = item.getString();

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
                            stopAll();
                            break;

                        case STOP:
                            item = msg.getPayloadItem("id");
                            if (item == null) {
                                System.out.println("Reject message, no id");
                                return;
                            }
                            int id = item.getInt();

                            stop(id);
                            break;

                        case DIE:
                            item = msg.getPayloadItem("killProcesses");
                            boolean killProcesses = false;
                            if (item != null) {
                                killProcesses = item.getInt() != 0;
                            }
                            if (killProcesses) stopAll();
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
            System.out.println("startProcess: io error gathering error output");
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
        int id;
        CommandInfo info;
        cMsgMessage responseMsg;

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
         *
         */
        private void startProcess() {
            //----------------------------------------------------------------
            // run the command
            //----------------------------------------------------------------
            Process process;
            try {
System.out.println("run -> " + info.command);
                process = Runtime.getRuntime().exec(info.command);
                // Allow process a chance to run before testing if its terminated.
                Thread.yield();
                try {Thread.sleep(300);}
                catch (InterruptedException e) {}

            }
            catch (Exception e) {
                e.printStackTrace();
                // return error message if execution failed
                try {
                    cMsgPayloadItem item1 = new cMsgPayloadItem("terminated", 1);
                    responseMsg.addPayloadItem(item1);
                    cMsgPayloadItem item2 = new cMsgPayloadItem("error", e.getMessage());
                    responseMsg.addPayloadItem(item2);
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
                    // Store Process id/object so Commander can terminate it later.
                    else {
                        int id = uniqueId.incrementAndGet();
                        try {
                            cMsgPayloadItem item = new cMsgPayloadItem("id", id);
                            responseMsg.addPayloadItem(item);
                        }
                        catch (cMsgException e) {/* never happen */}
                        info.process = process;
                        processMap.put(id, info);
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
            // If we're here, we want to wait until process is fnished.
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

            try {
                //----------------------------------------------------------------
                // Respond to initial sendAndGet if we haven't done so already
                // so "startProcess" can return if it has not timed out already
                // (in which case it is not interested in this result anymore).
                //----------------------------------------------------------------
                if (info.wait) {
                    cmsgConnection.send(responseMsg);
                    return;
                }

                //----------------------------------------------------------------
                // If we're here it's because the Commander is not synchronously
                // waiting, but has registered a callback that the next message
                // will trigger.

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
        int id;
        CommandInfo info;
        cMsgMessage responseMsg;

        ThreadRun(CommandInfo info, cMsgMessage responseMsg) {
            this.info = info;
            this.responseMsg = responseMsg;
        }

        public void run() {
            startThread();
        }

        /**
         * Recursive method that creates an object of type className from the cMsg message.
         *
         * @param msg cMsg message containing all data necessary to create object.
         * @param className name of the class of the created object.
         * @return created object.
         * @throws cMsgException if error in creating object.
         */
        Object createObjectFromMessage(cMsgMessage msg, String className) throws cMsgException {

            //----------------------------------------------------------------
            // create an object from the given class name
            //----------------------------------------------------------------
            Object returnObject;

            try {
                Class c = Class.forName(className);

                // if no args, use no-arg constructor
                if (msg == null) {
                    returnObject = c.newInstance();
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
                        // throw protocol violation error
                        throw new cMsgException("Protocol violation: classes and/or types arrays null");
                    }
                    String[] classNames = classesItem.getStringArray();
                    int[] argTypeValues = argsTypesItem.getIntArray();
                    if (classNames.length < numArgs || argTypeValues.length < numArgs) {
                        // throw protocol violation error
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
                            // throw protocol violation error
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
System.out.println("arg #" + i + " -> type = " + aType.name() + ", class = " + classNames[i]);
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
                        // throw protocol violation error
                    }
                    if (numMessageArgs > 0 &&
                            (messageValuesItem == null || messageValuesItem.getCount() < numMessageArgs)) {
                        // throw protocol violation error
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

                    for (int i=0; i < numArgs; i++) {
                        switch (argTypes[i]) {
                            case PRIMITIVE:
System.out.println("Create object of " + classNames[i] + " with arg " + stringArgs[stringArgIndex]);
                                Constructor con = argClasses[i].getConstructor(stringParam);
                                singleArg[0] = stringArgs[stringArgIndex++];
                                args[i] = con.newInstance(singleArg);
                                break;

                            case PRIMITIVE_CHAR:
System.out.println("Create object of " + classNames[i] + " with arg " + stringArgs[stringArgIndex]);
                                con = argClasses[i].getConstructor(charParam);
                                // change String to int to char to Character to Object (whew!)
                                singleArg[0] = (char) (Integer.parseInt(stringArgs[stringArgIndex++]));
                                args[i] = con.newInstance(singleArg);
                                break;

                            case REFERENCE:
                                // recursive stuff here
                                cMsgMessage argMsg = msgArgs[messageArgIndex++];
System.out.println("Create object of " + classNames[i] + " from msg ");
                                Object argObject = createObjectFromMessage(argMsg, classNames[i]);
                                args[i] = argObject;
                                break;

                            case REFERENCE_NOARG:
System.out.println("Use no-arg constructor of " + classNames[i]);
                                args[i] = argClasses[i].newInstance();
                                break;

                            case NULL:
System.out.println("Use null for arg #" + (i+1));
                                args[i] = null;
                                break;

                            default:
                                // throw error
                        }
                    }

                    // constructor required to have args
                    Constructor[] ca = c.getConstructors();
//                    for (Constructor cc : ca) {
//System.out.println("cc = " + cc + "\nargs = ");
//                        Class[] cl = cc.getParameterTypes();
//                        for (Class cl1 : cl) {
//                            System.out.print(cl1.getName() + " ");
//                        }
//                    }
                    Constructor co = c.getConstructor(parameterTypes);

                    // create an instance
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
         *
         */
        private void startThread() {
            //----------------------------------------------------------------
            // create an object from the given class name
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
                cmsgConnection.send(imDoneMsg);
            }
            catch (cMsgException e) {
                // sending msg failed due to broken connection to server, all bets off
            }
        }


    }







    /**
     *
     */
    private void stopAll() {
        for (CommandInfo info : processMap.values()) {
            // stop process
//System.out.println("Kill the process");
            info.process.destroy();
        }

        for (CommandInfo info : threadMap.values()) {
            // stop thread
//System.out.println("Kill the thread");
            // doesn't matter if alive or not
            info.execThread.shutItDown();
        }
    }


    /**
     *
     * @param id
     */
    private void stop(int id) {
        CommandInfo info = processMap.get(id);
        if (info == null) {
            info = threadMap.get(id);
        }
        if (info == null) {
            // process/thread already stopped
//System.out.println("No thread or process for that id");
            return;
        }

        // stop process
        if (info.isProcess) {
//System.out.println("Kill the process " + info.process);
            info.process.destroy();
        }
        // stop thread
        else {
//System.out.println("Kill the thread");
            // doesn't matter if alive or not
            info.execThread.shutItDown();
        }

    }

    private cMsgMessage createReturnMessage(InfoType type) {
        try {
            cMsgMessage msg = new cMsgMessage();
            msg.setHistoryLengthMax(0);
            msg.setType(name);
            msg.setSubject(type.getValue());
            return msg;
        }
        catch (cMsgException e) {/* never happen */}
        return null;
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
     */
    public static void main(String[] args) {
        try {
            String[] arggs = decodeCommandLine(args);
            System.out.println("Starting Executor with:\n  password = " + arggs[0] +
                               "\n  name = " + arggs[2] + "\n  udl = " + arggs[1]);
            new Executor(arggs[0], null, arggs[1], arggs[2]);
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
