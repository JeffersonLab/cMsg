package org.jlab.coda.cMsg.remoteExec;

import org.jlab.coda.cMsg.*;

import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.TimeoutException;
import java.util.ArrayList;
import java.util.List;
import java.io.BufferedReader;
import java.io.InputStreamReader;

/**
 * @author timmer
 * Date: Oct 13, 2010
 */
public class Commander {

    public static final String allType = ".all";
    public static final String remoteExecSubjectType = "cMsgRemoteExec";

    private String udl;
    private String myName;
    private String description;

    private int uniqueId;
    private cMsg cmsgConnection;
    private ConcurrentHashMap<String, ExecutorInfo> executors =
            new ConcurrentHashMap<String, ExecutorInfo>();


    /**
     * Create this object given all 3 parameters needed to make a connection to the
     * cMsg server.
     *
     * @param udl
     * @param name
     * @param description
     * @throws cMsgException
     */
    public Commander(String udl, String name, String description) throws cMsgException {
        connectToServer(udl, name, description);
        // once we're connected, see who's out there ...
        findExecutors();
    }


    /**
     * Tries to connect to the last cMsg server successfully connected to.
     *
     * @return <code>false</code> (nothing done) if already connected,
     *         <code>true</code> if a new connection is made.
     * @throws cMsgException if cannot connect.
     */
    public boolean reconnectToServer() throws cMsgException {
        if (cmsgConnection.isConnected()) {
            return false;
        }

        connectToServer(udl, myName, description);
        return true;
    }


    public void connectToServer(String udl, String name, String description) throws cMsgException {
        if (cmsgConnection != null && cmsgConnection.isConnected()) {
            cmsgConnection.disconnect();
        }

        cmsgConnection = new cMsg(udl, name, description);
        cmsgConnection.connect();
        cmsgConnection.start();
        this.udl = udl;
        this.myName = name;
        this.description = description;

        // subscription to read info from all responding executors
        System.out.println("Subscribe to sub = " + InfoType.REPORTING.getValue() + ", typ = " + remoteExecSubjectType);
        cmsgConnection.subscribe(InfoType.REPORTING.getValue(), remoteExecSubjectType,
                                 new FindExecutorsCallback(), null);

    }


    public void findExecutors() throws cMsgException {
System.out.println("findExecutors: in");
        if (!cmsgConnection.isConnected()) {
            throw new cMsgException("not connect to cMsg server");
        }

        executors.clear();

        // tell executors out there to respond ...
System.out.println("findExecutors: send msg to sub = " + remoteExecSubjectType + ", typ = " + allType);
        cMsgMessage msg = new cMsgMessage();
        msg.setHistoryLengthMax(0);
        msg.setSubject(remoteExecSubjectType);
        msg.setType(allType);
        cMsgPayloadItem item = new cMsgPayloadItem("commandType", CommandType.IDENTIFY.getValue());
        msg.addPayloadItem(item);
        cmsgConnection.send(msg);

        // wait 1/2 second for replies
        try { Thread.sleep(500); }
        catch (InterruptedException e) { }
System.out.println("findExecutors: done");
    }


    public List<ExecutorInfo> getExecutors() {
        return new ArrayList<ExecutorInfo>(executors.values());
    }


    /**
     * This class defines the callback to be run when a message
     * containing a valid command arrives.
     */
    class FindExecutorsCallback extends cMsgCallbackAdapter {

        public void callback(cMsgMessage msg, Object userObject) {
System.out.println("HEY GOT YOUR MESSY");
            // there must be payload
            if (msg.hasPayload()) {

                System.out.println("payload = ");
                msg.payloadPrintout(0);

                cMsgPayloadItem item;
                String name = null, os = null, machine = null, release = null, processor = null;

                try {
                    // name of responding executor
                    item = msg.getPayloadItem("name");
                    if (item != null) {
                        name = item.getString();
                        if (name == null) {
                            System.out.println("Reject message, bad format");
                            return;
                        }
                    }

                    // os of responding executor
                    item = msg.getPayloadItem("os");
                    if (item != null) {
                        os = item.getString();
                        if (os == null) {
                            System.out.println("Reject message, bad format");
                            return;
                        }
                    }

                    // machine hardware of responding executor
                    item = msg.getPayloadItem("machine");
                    if (item != null) {
                        machine = item.getString();
                        if (machine == null) {
                            System.out.println("Reject message, bad format");
                            return;
                        }
                    }

                    // processor of responding executor
                    item = msg.getPayloadItem("processor");
                    if (item != null) {
                        processor = item.getString();
                        if (processor == null) {
                            System.out.println("Reject message, bad format");
                            return;
                        }
                    }

                    // os release of responding executor
                    item = msg.getPayloadItem("release");
                    if (item != null) {
                        release = item.getString();
                        if (release == null) {
                            System.out.println("Reject message, bad format");
                            return;
                        }
                    }

                    // looks like we have a valid response, store it
                    ExecutorInfo eInfo = new ExecutorInfo(name, os, machine, processor, release);
                    executors.put(name, eInfo);
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
     * Kill the specified executor and, optionally, all processes it has started.
     *
     * @param exec object specifying executor.
     * @param killProcesses <code>false</code> to leave spawned processes running,
     *                      else <code>true</code> to kill them too.
     * @throws cMsgException
     */
    public void kill(ExecutorInfo exec, boolean killProcesses) throws cMsgException {
        cMsgMessage msg = new cMsgMessage();
        msg.setHistoryLengthMax(0);
        msg.setSubject(remoteExecSubjectType);
        msg.setType(exec.getName());
        cMsgPayloadItem item1 = new cMsgPayloadItem("commandType", CommandType.DIE.getValue());
        cMsgPayloadItem item2 = new cMsgPayloadItem("killProcesses", killProcesses ? 1 : 0);
        msg.addPayloadItem(item1);
        msg.addPayloadItem(item2);
        cmsgConnection.send(msg);
    }


    /**
     * Kill all executors and, optionally, all processes they have started.
     *
     * @param killProcesses <code>false</code> to leave spawned processes running,
     *                      else <code>true</code> to kill them too.
     * @throws cMsgException
     */
    public void killAll(boolean killProcesses) throws cMsgException {
        cMsgMessage msg = new cMsgMessage();
        msg.setHistoryLengthMax(0);
        msg.setSubject(remoteExecSubjectType);
        msg.setType(allType);
        cMsgPayloadItem item1 = new cMsgPayloadItem("commandType", CommandType.DIE.getValue());
        cMsgPayloadItem item2 = new cMsgPayloadItem("killProcesses", killProcesses ? 1 : 0);
        msg.addPayloadItem(item1);
        msg.addPayloadItem(item2);
        cmsgConnection.send(msg);
    }


    /**
     *
     * @param exec
     * @throws cMsgException
     */
    public void stop(ExecutorInfo exec, int id) throws cMsgException {
        cMsgMessage msg = new cMsgMessage();
        msg.setHistoryLengthMax(0);
        msg.setSubject(remoteExecSubjectType);
        msg.setType(exec.getName());
        cMsgPayloadItem item1 = new cMsgPayloadItem("commandType", CommandType.STOP_ALL.getValue());
        msg.addPayloadItem(item1);

        // use commander id to retrieve executor id (id meaningful to executor) and remove from storage
        Integer execId = exec.removeCommanderId(id);
        if (execId == null) {
            // no such id exists for this Executor
            throw new cMsgException("no such id exists");
        }
        cMsgPayloadItem item2 = new cMsgPayloadItem("id", execId);

        msg.addPayloadItem(item2);
        cmsgConnection.send(msg);
    }

    // todo: expand API to lookup using executor name instead of object
    /**
     *
     * @throws cMsgException
     */
    public void stopAll(ExecutorInfo exec) throws cMsgException {
        cMsgMessage msg = new cMsgMessage();
        msg.setHistoryLengthMax(0);
        msg.setSubject(remoteExecSubjectType);
        msg.setType(exec.getName());
        cMsgPayloadItem item1 = new cMsgPayloadItem("commandType", CommandType.STOP_ALL.getValue());
        msg.addPayloadItem(item1);
        cmsgConnection.send(msg);
        // no threads or processes will be running after this command
        exec.clearIds();
    }

    /**
     *
     * @throws cMsgException
     */
    public void stopAll() throws cMsgException {
        cMsgMessage msg = new cMsgMessage();
        msg.setHistoryLengthMax(0);
        msg.setSubject(remoteExecSubjectType);
        msg.setType(allType);
        cMsgPayloadItem item1 = new cMsgPayloadItem("commandType", CommandType.STOP_ALL.getValue());
        msg.addPayloadItem(item1);
        cmsgConnection.send(msg);
        for (ExecutorInfo exec : executors.values()) {
            exec.clearIds();
        }
    }


    synchronized private int getUniqueId() {
        return uniqueId++;
    }


    /**
     * Start an external process using the specified executor.
     *
     * @param exec Executor to start process with.
     * @param cmd command that Executor will run.
     * @param monitor <code>true</code> if output of the command should be captured and returned,
     *                else <code>false</code>.
     * @param wait <code>true</code> if executor waits for the process to complete before responding,
     *             else <code>false</code>.
     * @param timeout milliseconds to wait for reply (coming via asynchronous messaging system),
     *                0 means wait forever.
     * @return object containing id number and any process output captured
     * @throws cMsgException if cmsg communication fails or internal protocol error
     * @throws TimeoutException if cmsg communication times out
     */
    public CommandReturn startProcess(ExecutorInfo exec, String cmd, boolean monitor, boolean wait, int timeout)
            throws cMsgException, TimeoutException {

        int myId = getUniqueId();
        String output = null;

        cMsgMessage msg = new cMsgMessage();
        msg.setHistoryLengthMax(0);
        msg.setSubject(remoteExecSubjectType);
        msg.setType(exec.getName());

        try {
            cMsgPayloadItem item1 = new cMsgPayloadItem("commandType", CommandType.START_PROCESS.getValue());
            msg.addPayloadItem(item1);
            cMsgPayloadItem item2 = new cMsgPayloadItem("command", cmd);
            msg.addPayloadItem(item2);
            cMsgPayloadItem item3 = new cMsgPayloadItem("monitor", monitor ? 1 : 0);
            msg.addPayloadItem(item3);
            cMsgPayloadItem item4 = new cMsgPayloadItem("wait", wait ? 1 : 0);
            msg.addPayloadItem(item4);
        }
        catch (cMsgException e) {/* never happen as names are legit */}

        // send msg and receive response
System.out.println("Doing sendAndGet to sub = " + remoteExecSubjectType + ", typ = " + exec.getName());
        cMsgMessage returnMsg = cmsgConnection.sendAndGet(msg, timeout);

        // analyze response
        if (returnMsg.hasPayload()) {
            // Was there an error?
            cMsgPayloadItem item = returnMsg.getPayloadItem("error");
            if (item != null) {
                String err = item.getString();            
                return new CommandReturn(myId, 0, true, true, err);
            }

            // Has the process already terminated?
            boolean terminated = false;
            item = returnMsg.getPayloadItem("terminated");
            if (item != null) {
                terminated = item.getInt() != 0;
            }

            // If it hasn't, get its id
            int processId = 0;
            if (!terminated) {
                item = returnMsg.getPayloadItem("id");
                if (item == null) {
                    throw new cMsgException("startProcess: internal protocol error");
                }
                processId = item.getInt();
                // Store mapping between the 2 ids to help terminating it in future.
                exec.addCommanderId(myId, processId);
            }

            // if we requested the output of the process ...
            if (monitor) {
                item = returnMsg.getPayloadItem("output");
                if (item != null) {
                    output = item.getString();
                }
            }

            return new CommandReturn(myId, processId, false, terminated, output);
        }
        else {
            throw new cMsgException("startProcess: internal protocol error");
        }
    }


    /**
     * Start an internal thread in the specified executor.
     *
     * @param exec Executor to start thread in.
     * @param className name of java class to instantiate and run as thread in executor.
     * @param timeout milliseconds to wait for reply (coming via asynchronous messaging system),
     *                0 means wait forever.
     * @return id number of this operation
     * @throws cMsgException if cmsg communication fails or internal protocol error
     * @throws TimeoutException if cmsg communication times out
     */
    public CommandReturn startThread(ExecutorInfo exec, String className, int timeout)
            throws cMsgException, TimeoutException {

        int threadId;
        int myId = getUniqueId();

        cMsgMessage msg = new cMsgMessage();
        msg.setHistoryLengthMax(0);
        msg.setSubject(remoteExecSubjectType);
        msg.setType(exec.getName());

        try {
            cMsgPayloadItem item1 = new cMsgPayloadItem("commandType", CommandType.START_THREAD.getValue());
            msg.addPayloadItem(item1);
            cMsgPayloadItem item2 = new cMsgPayloadItem("className", className);
            msg.addPayloadItem(item2);
        }
        catch (cMsgException e) {/* never happen as names are legit */}

        // send msg and receive response
        cMsgMessage returnMsg = cmsgConnection.sendAndGet(msg, timeout);

        // analyze response
        if (returnMsg.hasPayload()) {
            // Was there an error?
            cMsgPayloadItem item = msg.getPayloadItem("error");
            if (item != null) {
                String err = item.getString();
                return new CommandReturn(myId, 0, true, true, err);
            }

            item = msg.getPayloadItem("id");
            if (item == null) {
                throw new cMsgException("startProcess: internal protocol error");
            }
            threadId = item.getInt();

            // store mapping between the 2 ids
            exec.addCommanderId(myId, threadId);
            return new CommandReturn(myId, threadId, false, false, null);
        }
        else {
            throw new cMsgException("startProcess: internal protocol error");
        }
    }

//    public int startSerializedThread(String className) throws cMsgException {
//        return 0;
//    }

    private static String inputStr(String s) {
        String aLine = "";
        BufferedReader input = new BufferedReader(new InputStreamReader(System.in));
        System.out.print(s);
        try {
            aLine = input.readLine();
        }
        catch (Exception e) {
            e.printStackTrace();
        }
        return aLine;
    }

    /**
     * Method to decode the command line used to start this application.
     * @param args command line arguments
     */
    private static String[] decodeCommandLine(String[] args) {
        String[] stuff = new String[2];

        // loop over all args
        for (int i = 0; i < args.length; i++) {
            if (args[i].equalsIgnoreCase("-n")) {
                stuff[1] = args[i + 1];  // name
                i++;
            }
            else if (args[i].equalsIgnoreCase("-u")) {
                stuff[0]= args[i + 1];   // udl
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
            System.out.println("Starting Executor with:\n  name = " + arggs[1] + "\n  udl = " + arggs[0]);
            Commander cmdr = new Commander(arggs[0], arggs[1], "commander");
            //cmdr.findExecutors();   // already done in constructor
            List<ExecutorInfo> execList = cmdr.getExecutors();
            System.out.println("execList =  "+ execList);
            for (ExecutorInfo info : execList) {
                System.out.println("Found executor: name = " + info.getName() + " running on " + info.getOS());
            }

            String in;
            while(true) {
                in = inputStr("% ");
                if (execList.size() > 0) {
                    CommandReturn ret = cmdr.startProcess(execList.get(0), in, true, true, 1000);
                    if (ret.getOutput() != null)
                        System.out.println(ret.getOutput());
                }
            }

            //cmdr.kill(execList.get(0), true);
        }
        catch (TimeoutException e) {
            e.printStackTrace();
            System.exit(-1);
        }
        catch (cMsgException e) {
            e.printStackTrace();
            System.exit(-1);
        }
    }

}
