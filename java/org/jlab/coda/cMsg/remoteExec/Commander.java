package org.jlab.coda.cMsg.remoteExec;

import org.jlab.coda.cMsg.*;

import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.TimeoutException;
import java.util.ArrayList;
import java.util.List;
import java.util.LinkedList;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.awt.*;

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

    private ConcurrentHashMap<Integer, CommandReturn> cmdReturns =
            new ConcurrentHashMap<Integer, CommandReturn>();


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
        findExecutors(500);
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
        cmsgConnection.subscribe(myName, remoteExecSubjectType,
                                 new CommandResponseCallback(), null);

    }


    public void findExecutors(int milliseconds) throws cMsgException {
        if (!cmsgConnection.isConnected()) {
            throw new cMsgException("not connect to cMsg server");
        }

        executors.clear();

        // tell executors out there to respond ...
//System.out.println("findExecutors: send msg to sub = " + remoteExecSubjectType + ", typ = " + allType);
        cMsgMessage msg = new cMsgMessage();
        msg.setHistoryLengthMax(0);
        msg.setSubject(remoteExecSubjectType);
        msg.setType(allType);
        cMsgPayloadItem item1 = new cMsgPayloadItem("commandType", CommandType.IDENTIFY.getValue());
        msg.addPayloadItem(item1);
        cMsgPayloadItem item2 = new cMsgPayloadItem("commander", myName);
        msg.addPayloadItem(item2);
        cmsgConnection.send(msg);

        // wait for replies
        try { Thread.sleep(milliseconds); }
        catch (InterruptedException e) { }
    }


    public List<ExecutorInfo> getExecutors() {
        return new ArrayList<ExecutorInfo>(executors.values());
    }


    /**
     * This class defines the callback to be run when a message
     * containing a valid command arrives.
     */
    class CommandResponseCallback extends cMsgCallbackAdapter {

        public void callback(cMsgMessage msg, Object userObject) {
            // there must be a payload
            if (msg.hasPayload()) {

                cMsgPayloadItem item;

                try {

                    // name of return information type
                    item = msg.getPayloadItem("returnType");
                    String returnType = null;
                    if (item != null) {
                        returnType = item.getString();
                        if (returnType == null) {
                            System.out.println("Reject message, bad format");
                            return;
                        }
                    }
                    InfoType type = InfoType.getInfoType(returnType);
                    if (type == null) {
                        System.out.println("Reject message, return type not recognized");
                        return;
                    }

                    switch (type) {
                        // process telling us it's done
                        case PROCESS_END:
                            item = msg.getPayloadItem("id");
                            if (item == null) {
                                System.out.println("Reject message, no commander id");
                                return;
                            }
                            int id = item.getInt();
                            for (ExecutorInfo exec : executors.values()) {
                                exec.removeCommanderId(id);
                            }

                            CommandReturn cmdRet = cmdReturns.remove(id);
                            if (cmdRet != null) {
                                cmdRet.hasTerminated(true);
                                cmdRet.executeCallback();
                            }

                            break;

                        // executor responding to "identify" command
                        case REPORTING:
                            String name = null, os = null, machine = null,
                                   release = null, processor = null;

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
                            break;
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
        // use commander id to retrieve executor id (id meaningful to executor) and remove from storage
        Integer execId = exec.removeCommanderId(id);
        if (execId == null) {
            // no such id exists for this Executor since it was already terminated
            return;
        }

        cMsgMessage msg = new cMsgMessage();
        msg.setHistoryLengthMax(0);
        msg.setSubject(remoteExecSubjectType);
        msg.setType(exec.getName());
        cMsgPayloadItem item1 = new cMsgPayloadItem("commandType", CommandType.STOP.getValue());
        msg.addPayloadItem(item1);
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
     * Start an xterm with a process using the specified executor.
     * Do not wait for it to exit. Allows 2 seconds for return message
     * from executor before throwing exeception. Reading from xterm
     * output blocks for some reason so no monitoring of it.
     *
     * @param exec Executor to start process with.
     * @param cmd command that Executor will run inside xterm. Can be null.
     * @param geometry geometry info in the form WxH+-X+-Y with W & H in chars, X & Y in pixels;
     *                 for example 75x10+0+200 or 75x10-10-20. Can be null.
     * @param title window title, can be null.
     * @return object containing id number and any process output captured
     * @throws cMsgException if cmsg communication fails or takes too long, or internal protocol error
     */
    public CommandReturn startXtermProcess(ExecutorInfo exec, String cmd, String geometry, String title)
            throws cMsgException, TimeoutException {

        String realCmd = "xterm -sb ";
        if (geometry != null) {
            realCmd += " -geometry " + geometry;
        }
        if (title != null) {
            realCmd += " -T " + title;
        }
        if (cmd != null && cmd.length() > 0) {
            realCmd += " -e " + cmd;
        }
        return startProcess(exec, realCmd, false, null, null);
    }


    /**
     * Start an external process using the specified executor. Do not wait for it to finish
     * but run callback when it does. Allows 2 seconds for return message from executor
     * before throwing exeception.
     *
     * @param exec Executor to start process with.
     * @param cmd command that Executor will run.
     * @param monitor <code>true</code> if output of the command should be captured and returned,
     *                else <code>false</code>.
     * @param callback callback to be run when process ends.
     * @param userObject argument to be passed to callback.
     * @return object containing id number and any process output captured
     * @throws cMsgException if cmsg communication fails or takes too long, or internal protocol error
     */
    public CommandReturn startProcess(ExecutorInfo exec, String cmd, boolean monitor,
                                      ProcessCallback callback, Object userObject)
            throws cMsgException {

        try {
            return startProcess(exec, cmd, monitor, false, callback, userObject, 2000);
        }
        catch (TimeoutException e) {
            throw new cMsgException(e);
        }
    }


    /**
     * Start an external process using the specified executor. Wait for it to finish.
     *
     * @param exec Executor to start process with.
     * @param cmd command that Executor will run.
     * @param monitor <code>true</code> if output of the command should be captured and returned,
     *                else <code>false</code>.
     * @param timeout milliseconds to wait for reply (coming via asynchronous messaging system),
     *                0 means wait forever.
     * @return object containing id number and any process output captured
     * @throws cMsgException if cmsg communication fails or internal protocol error
     * @throws TimeoutException if cmsg communication times out
     */
    public CommandReturn startProcess(ExecutorInfo exec, String cmd, boolean monitor, int timeout)
            throws cMsgException, TimeoutException {

        return startProcess(exec, cmd, monitor, true, null, null, timeout);
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
     * @param callback callback to be run when process ends.
     * @param userObject argument to be passed to callback.
     * @param timeout milliseconds to wait for reply (coming via asynchronous messaging system),
     *                0 means wait forever.
     * @return object containing id number and any process output captured
     * @throws cMsgException if cmsg communication fails or internal protocol error
     * @throws TimeoutException if cmsg communication times out
     */
    public CommandReturn startProcess(ExecutorInfo exec, String cmd, boolean monitor, boolean wait,
                                      ProcessCallback callback, Object userObject, int timeout)
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
            cMsgPayloadItem item5 = new cMsgPayloadItem("commander", myName); // cmsg "address" subject
            msg.addPayloadItem(item5);
            cMsgPayloadItem item6 = new cMsgPayloadItem("id", myId);  // send this back when process done
            msg.addPayloadItem(item6);
        }
        catch (cMsgException e) {/* never happen as names are legit */}

        // send msg and receive response
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

            CommandReturn cmdRet = new CommandReturn(myId, processId, false, terminated, output);
            // register callback for execution on process termination
            if (!terminated && callback != null) {
                cmdRet.registerCallback(callback, userObject);
                cmdReturns.put(myId, cmdRet);
            }

            return cmdRet;
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
        System.out.flush();
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
     * Example method of how to make a bunch of identical xterm windows fill the screen.
     *
     * @param exec Executor to use.
     * @param cmdr Commander to use.
     * @param count number of windows to make.
     * @param widthChars width of each xterm in characters.
     * @param heightChars number of lines in each xterm.
     *
     * @return list of CommandReturn objects.
     *
     * @throws cMsgException
     * @throws TimeoutException
     */
    public List<CommandReturn> startWindows(ExecutorInfo exec, Commander cmdr, int count, int widthChars, int heightChars)
            throws cMsgException, TimeoutException {

        // Get info about our display.
        Rectangle rec = GraphicsEnvironment.getLocalGraphicsEnvironment().getMaximumWindowBounds();

        // Where do we start?
        int xInit = 0;
        int yInit = 25;  // avoid Redhat 5 top bar

        // How big is our screen in pixels?
        int screenWidth  = rec.width  - xInit;
        int screenHeight = rec.height - yInit;

        // How big is the xterm window in pixels?
        int xtermHeight = 27 + 14*heightChars;
        int xtermWidth  = 30 +  6*widthChars;

        // Where do we start next window?
        int startX = xInit;
        int startY = yInit;

        // Do we have room for another xterm?
        boolean stillRoomY = true, stillRoomX = true;

        String geo;
        String title = "hey";
        String geoSize = widthChars+"x"+heightChars;
        LinkedList<CommandReturn> returnList = new LinkedList<CommandReturn>();

        // Make some windows
        while (stillRoomY || stillRoomX) {
            geo = geoSize + "+" + startX + "+" + startY;
            startY += xtermHeight;

            // create xterm and add return object to list
            returnList.add(cmdr.startXtermProcess(exec, null, geo, title));

            // Reached our count.
            if (--count < 1) {
                break;
            }

            // Did we run out of vertical space?
            if (startY > screenHeight - xtermHeight) {
                stillRoomY = false;
            }

            // Did we run out of horizontal space?
            if (startX > screenWidth - xtermWidth) {
                stillRoomX = false;
            }

            // Do we start a new column?
            if (stillRoomX && !stillRoomY) {
                startX += xtermWidth;
                if (startX > screenWidth - xtermWidth) {
                    stillRoomX = false;
                }
                else {
                    startY = yInit;
                    stillRoomY = true;
                }
            }
        }

        return returnList;
    }

    public static void main(String[] args) {

        try {
            String[] arggs = decodeCommandLine(args);
            System.out.println("Starting Executor with:\n  name = " + arggs[1] + "\n  udl = " + arggs[0]);
            Commander cmdr = new Commander(arggs[0], arggs[1], "commander");
            List<ExecutorInfo> execList = cmdr.getExecutors();
            for (ExecutorInfo info : execList) {
                System.out.println("Found executor: name = " + info.getName() + " running on " + info.getOS());
            }

            if (execList.size() > 0) {
                List<CommandReturn> retList = cmdr.startWindows(execList.get(0), cmdr, 10, 85, 8);
                for (CommandReturn ret : retList) {
                    cmdr.stop(execList.get(0), ret.getId());
                    try {Thread.sleep(500);}
                    catch (InterruptedException e) {}
                }
            }
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

    /**
     * Run as a stand-alone application
     */
    public static void main3(String[] args) {
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
//
//            for (ExecutorInfo info : execList) {
//                System.out.println("Killing you " + info.getName());
//                cmdr.kill(info, true);
//            }
//            System.out.println("Killing all");
//            cmdr.killAll(true);
//            if (execList.size() > 0) {
//                CommandReturn ret = cmdr.startProcess(execList.get(0), "java org/jlab/coda/cMsg/test/cMsgTest", false, false, 1000);
//                    if (ret.getOutput() != null)
//                        System.out.println(ret.getOutput());
//
//            }

            class myCB implements ProcessCallback {
                public void callback(Object userObject) {
                    System.out.println("\nHEY, CALLBACK RUN!!!\n");
                }
            }

            String in;
            while(true) {
                try {
                    Thread.sleep(2000);
                }
                catch (InterruptedException e) {
                    break;
                }
                in = inputStr("% ");
                if (execList.size() > 0) {
                    CommandReturn ret = cmdr.startProcess(execList.get(0), in, false, false, new myCB(), null, 30000);
                    System.out.println("Return = \n" + ret);
                    if (ret.hasError()) {
                        System.out.println("@@@@@@@ ERROR @@@@@@@");
                    }
                    if (ret.getOutput() != null) {
                        System.out.println(ret.getOutput());
                    }
//                    try {Thread.sleep(5000);}
//                    catch (InterruptedException e) {}
//
//                    cmdr.stop(execList.get(0), ret.getId());

//                    while (true) {
//                        try {Thread.sleep(1000);}
//                        catch (InterruptedException e) {}
//                    }


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
