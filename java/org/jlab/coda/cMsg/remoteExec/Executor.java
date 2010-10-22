package org.jlab.coda.cMsg.remoteExec;

import org.jlab.coda.cMsg.*;

import javax.crypto.SecretKey;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.BufferedReader;
import java.io.InputStream;
import java.util.HashMap;
import java.net.InetAddress;
import java.net.UnknownHostException;

/**
 * This class, when used appropriately, can execute any command on any node by
 * being sent the proper cMsg message.
 *
 * @author timmer
 * Date: Oct 7, 2010
 */
public class Executor {

    public static final String allType = ".all";
    public static final String remoteExecSubjectType = "cMsgRemoteExec";

    private SecretKey key;

    private int uniqueId = 1;

    private String password;
    private String name;
    private String udl;
    private String os;         // uname -s   operating system
    private String machine;    // uname -m   machine hardware
    private String processor;  // uname -p   processor type
    private String release;    // uname -r   operating system release

    private HashMap<Integer, CommandInfo> processMap = new HashMap<Integer, CommandInfo>(100);
    private HashMap<Integer, CommandInfo>  threadMap = new HashMap<Integer, CommandInfo>(100);
    private cMsg cmsgConnection;
    private volatile boolean connected;


    private class CommandInfo {
        ExecutorThread execThread;
        Process process;
        String  className;
        String  command;
        String  commander;
        int     commandId;
        boolean monitor;
        boolean wait;
        boolean isProcess;
    }

    private class ProcessEnd extends Thread {
        int id;
        CommandInfo info;
        ProcessEnd(CommandInfo info) {
            this.info = info;
        }
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

        // run some uname commands to find out system information
        try {
            Process pr = Runtime.getRuntime().exec("uname -s");
            BufferedReader br = new BufferedReader(new InputStreamReader(pr.getInputStream()));
            os = br.readLine();

            pr = Runtime.getRuntime().exec("uname -m");
            br = new BufferedReader(new InputStreamReader(pr.getInputStream()));
            machine = br.readLine();

            pr = Runtime.getRuntime().exec("uname -p");
            br = new BufferedReader(new InputStreamReader(pr.getInputStream()));
            processor = br.readLine();

            pr = Runtime.getRuntime().exec("uname -r");
            br = new BufferedReader(new InputStreamReader(pr.getInputStream()));
            release = br.readLine();

        } catch (IOException e) {
            // return error message if execution failed
            return;
        }

        // start thread to maintain connection with cmsg server
        CmsgConnectionHandler handler = new CmsgConnectionHandler();

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
System.out.println("Subscribe to sub = " + remoteExecSubjectType + ", typ = " + name);
                        cmsgConnection.subscribe(remoteExecSubjectType, name,  cb, null);
System.out.println("Subscribe to sub = " + remoteExecSubjectType + ", typ = " + allType);
                        cmsgConnection.subscribe(remoteExecSubjectType, allType, cb, null);
                        cmsgConnection.start();
                        connected = true;
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
                String commandType = null;

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
                            //startProcess(commandInfo, msg.response());

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

                            // return must be placed in sendAndGet response msg
                            startThread(commandInfo, msg.response());
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
                            sendBackInfo(commander);
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



    synchronized private int getUniqueId() {
        return uniqueId++;
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
            String output;
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
                        int id = getUniqueId();
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
                imDoneMsg.setType(remoteExecSubjectType);
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
     *
     * @param info
     */
    private void startThread(CommandInfo info, cMsgMessage responseMsg) {
        //----------------------------------------------------------------
        // create an object from the given class name
        //----------------------------------------------------------------
        IExecutorThread eThread;
        try {
            Class c = Class.forName(info.className);
            eThread = (IExecutorThread)c.newInstance(); // use no-arg constructor
        }
        catch (Exception e) {
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
        // Make an actual Thread out of a runnable object since the object
        // may or may not be a thread and we need to run it as one.
        //----------------------------------------------------------------
        ExecutorThread thread = new ExecutorThread(eThread);
        thread.start();

        //----------------------------------------------------------------
        // Store ID so we can terminate thread/executor later
        //----------------------------------------------------------------
        int id = getUniqueId();
        try {
            cMsgPayloadItem item = new cMsgPayloadItem("id", id);
            responseMsg.addPayloadItem(item);
        }
        catch (cMsgException e) {/* never happen */}

        info.execThread = thread;
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
            thread.join();
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
            imDoneMsg.setType(remoteExecSubjectType);
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
System.out.println("Kill the thread");
            // doesn't matter if alive or not
            info.execThread.interrupt();
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
System.out.println("Kill the thread");
            // doesn't matter if alive or not
            info.execThread.interrupt();
            info.execThread.cleanUp();
        }

    }

    private void sendBackInfo(String commander) throws cMsgException {
        cMsgMessage msg = new cMsgMessage();
        msg.setHistoryLengthMax(0);
//System.out.println("Send to sub = " + InfoType.REPORTING.getValue() + ", typ = " + remoteExecSubjectType);
        // This msg goes back only to the Commander that sent an "identify" command
        msg.setSubject(commander);
        msg.setType(remoteExecSubjectType);

        cMsgPayloadItem item0 = new cMsgPayloadItem("returnType", InfoType.REPORTING.getValue());
        msg.addPayloadItem(item0);

        cMsgPayloadItem item1 = new cMsgPayloadItem("name", name);
        msg.addPayloadItem(item1);
        cMsgPayloadItem item2 = new cMsgPayloadItem("os", os);
        msg.addPayloadItem(item2);
        cMsgPayloadItem item3 = new cMsgPayloadItem("machine", machine);
        msg.addPayloadItem(item3);
        cMsgPayloadItem item4 = new cMsgPayloadItem("processor", processor);
        msg.addPayloadItem(item4);
        cMsgPayloadItem item5 = new cMsgPayloadItem("release", release);
        msg.addPayloadItem(item5);
        cmsgConnection.send(msg);
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
            Executor exec = new Executor(arggs[0], null, arggs[1], arggs[2]);
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
