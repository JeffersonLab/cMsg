package org.jlab.coda.cMsg.remoteExec;

import org.jlab.coda.cMsg.*;

import javax.crypto.SecretKey;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.BufferedReader;
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

    private int uniqueId;

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
        IExecutorThread  execThread;
        Thread  thread;
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

                            item = msg.getPayloadItem("className");
                            if (item == null || item.getString() == null) {
                                System.out.println("Reject message, no class name");
                                return;
                            }
                            String className = item.getString();

                            commandInfo = new CommandInfo();
                            commandInfo.className = className;
                            // don't send back any output since it cannot be isolated to any 1 thread
                            commandInfo.monitor = false;
                            commandInfo.isProcess = false;

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
//System.out.println("******** TRY TO STOP ID = " + id);

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
         *
         */
        private void startProcess() {
            // this sendAndGet response needs a subject and type to be sent
//System.out.println("response msg subject = " + responseMsg.getSubject());
            responseMsg.setSubject("blah");
            responseMsg.setType("blah");

            // run the command
            Process process;
            try {
    System.out.println("run -> " + info.command);
                process = Runtime.getRuntime().exec(info.command);
                try {Thread.sleep(1000);}
                catch (InterruptedException e) {}
            }
            catch (IOException e) {
                e.printStackTrace();
                // return error message if execution failed
                try {
                    cMsgPayloadItem item = new cMsgPayloadItem("error", e.getMessage());
                    responseMsg.addPayloadItem(item);
                    cmsgConnection.send(responseMsg);
                }
                catch (cMsgException e1) {
                    // sending msg failed due to broken connection to server, all bets off
                }
                return;
            }

            // capture process output if desired
            if (info.monitor) {
                StringBuilder  sb = new StringBuilder(100);
                BufferedReader br = new BufferedReader(new InputStreamReader(process.getInputStream()));
                try {
                    String line;
                    // grab each line of text (without line terminator)
//System.out.println("WILL READ");
                    while ((line = br.readLine()) != null) {
                        sb.append(line);
                        sb.append("\n");
                    }
//System.out.println("DONE READING");
                }
                catch (IOException e) {
                    // probably best to ignore this error
                    System.out.println("startProcess: io error gathering output");
                }

                // send back any output
                if (sb.length() > 0) {
                    cMsgPayloadItem item = null;
                    try {
                        // take off last \n we put in buffer
                        sb.deleteCharAt(sb.length()-1);
                        item = new cMsgPayloadItem("output", sb.toString());
//System.out.println("output = " + sb.toString());
                    }
                    catch (cMsgException e) {/* never happen */}
                    responseMsg.addPayloadItem(item);
                }
            }

            // try to figure out if it has already terminated
            boolean terminated = true;
            try {
                if (info.wait) {
                    process.waitFor();
                }
                process.exitValue();
            }
            catch (Exception e) {
                terminated = false;
            }
//System.out.println("Terminated = " + terminated);

            if (terminated) {
                try {
                    cMsgPayloadItem item = new cMsgPayloadItem("terminated", 1);
                    responseMsg.addPayloadItem(item);
                    cmsgConnection.send(responseMsg);
                }
                catch (cMsgException e) {
                    // sending msg failed due to broken connection to server, all bets off
                }
                return;
            }

            // if we're here, the process is still running ...

            // store it so we can terminate it later
            int id = getUniqueId();
            try {
//System.out.println("******** ID = " + id);
                cMsgPayloadItem item = new cMsgPayloadItem("id", id);
                responseMsg.addPayloadItem(item);
            }
            catch (cMsgException e) {/* never happen */}
            info.process = process;
            processMap.put(id, info);

            try {
                // response to initial sendAndGet so "startProcess" can return
//System.out.println("Send sendAndGet response");
                cmsgConnection.send(responseMsg);

                // wait for the process to finish
                try {
//System.out.println("Wait 4 process to end");
                    process.waitFor();
                }
                catch (InterruptedException e) {}

                // now send another (regular) msg back to Commander
                // to notify it that the process is done
                cMsgMessage imDoneMsg = new cMsgMessage();
                imDoneMsg.setSubject(info.commander);
                imDoneMsg.setType(remoteExecSubjectType);
                cMsgPayloadItem item1 = new cMsgPayloadItem("returnType", InfoType.PROCESS_END.getValue());
                imDoneMsg.addPayloadItem(item1);
                cMsgPayloadItem item2 = new cMsgPayloadItem("id", info.commandId);
                imDoneMsg.addPayloadItem(item2);
//System.out.println("**************** SEND - ending msg *************");
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
    private void startThread(CommandInfo info, cMsgMessage msg) {
        // create an object from the given class name
        IExecutorThread eThread = null;
        try {
            Class c = Class.forName(info.className);
            eThread = (IExecutorThread)c.newInstance(); // use no-arg constructor
        }
        catch (Exception e) {
            // return error if object creation failed
            try {
                cMsgPayloadItem item = new cMsgPayloadItem("error", e.getMessage());
                msg.addPayloadItem(item);
                cmsgConnection.send(msg);
            }
            catch (cMsgException e1) {
                // sending msg failed due to broken connection to server, all bets off
            }
            return;
        }

        // Make an actual Thread out of a runnable object since the object
        // may or may not be a thread and we need to run it as one.
        Thread thread = new Thread(eThread);
        thread.start();

        // store it so we can terminate it later
        int id = getUniqueId();
        try {
            cMsgPayloadItem item = new cMsgPayloadItem("id", id);
            msg.addPayloadItem(item);
        }
        catch (cMsgException e) {/* never happen */}
        info.thread = thread;
        threadMap.put(id, info);

        // send msg back to Commander
        try {
            cmsgConnection.send(msg);
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
            System.out.println("Kill the process");
            info.process.destroy();
        }

        for (CommandInfo info : threadMap.values()) {
            // stop thread
            System.out.println("Kill the thread");
            // doesn't matter if alive or not
            info.thread.interrupt();
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
            System.out.println("No thread or process for that id");
            return;
        }

        // stop process
        if (info.isProcess) {
            System.out.println("Kill the process " + info.process);
            info.process.destroy();
        }
        // stop thread
        else {
            System.out.println("Kill the thread");
            // doesn't matter if alive or not
            info.thread.interrupt();
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
