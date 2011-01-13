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

import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.ArrayList;
import java.util.List;
import java.util.Collection;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.awt.*;

/**
 * This class in conjunction with an Executor running on a remote host,
 * allow its user to run any command or java thread on that Executor.
 *
 * @author timmer
 * Date: Oct 13, 2010
 */
public class Commander {
    /** cMsg message subject or type used in internal Commander/Executor communication. */
    static final String allSubjectType = ".all";
    /** cMsg message subject or type used in internal Commander/Executor communication. */
    static final String remoteExecSubjectType = "cMsgRemoteExec";

    /** UDL for connecting to cMsg server. */
    private String udl;
    /** Client name for connecting to cMsg server. */
    private String myName;
    /** Client description for connecting to cMsg server. */
    private String description;
    /** Connection to cMsg server. */
    private cMsg cmsgConnection;

    /** Encrypted password needed to talk to Executor. */
    private String password;

    /** Length of actual (non-encrypted) password needed to talk to Executor. */
    private int passwordLength;

    /**
     * Used to generate unique id numbers in a thread-safe manner
     * which are used to identify a startProcess or startThread action.
     * This id is used to run any registered callbacks or to stop
     * started processes and threads.
     */
    private AtomicInteger uniqueId = new AtomicInteger(1);

    /** Map of all known Executors indexed by their name (also their hostname). */
    private ConcurrentHashMap<String, ExecutorInfo> executors =
            new ConcurrentHashMap<String, ExecutorInfo>();

    /**
     * Map of all returned objects from started processes and threads indexed
     * by their id numbers. These objects can be used by the caller to check the
     * status and output of a command or thread. They are also passed to callbacks
     * and can be used to stop processes and threads.
     */
    private ConcurrentHashMap<Integer, CommandReturn> cmdReturns =
            new ConcurrentHashMap<Integer, CommandReturn>();


    /**
     * Create this object given parameters needed to make a connection to the
     * cMsg server.
     *
     * @param udl   UDL to connect to cMsg server.
     * @param name  unique name used to connect to cMsg server.
     * @param password  unique string used to connect to cMsg server.
     *                  It may be null but must be <= 16 characters if not null.
     * @param description description used to connect to cMsg server.
     * @throws cMsgException if error connecting to cMsg server; password is > 16 characters
     */
    public Commander(String udl, String name, String description, String password) throws cMsgException {
        if (password != null) {
            if (password.length() > 16) {
                throw new cMsgException("Password must be <= 16 characters");
            }
            this.password = ExecutorSecurity.encrypt(password);
            this.passwordLength = password.length();
        }

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

    /**
     * Connect to the specified cMsg server.
     *
     * @param udl   UDL to connect to cMsg server.
     * @param name  unique name used to connect to cMsg server.
     * @param description description used to connect to cMsg server.
     * @throws cMsgException if cannot connect.
     */
    public void connectToServer(String udl, String name, String description) throws cMsgException {
        if (cmsgConnection != null && cmsgConnection.isConnected()) {
            try {
                cmsgConnection.disconnect();
            }
            catch (cMsgException e) {/* ignore since it means we're disconneced anyway */}
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

        // the only msg coming to allSubjectType is REPORTING info from new executors starting up
        cmsgConnection.subscribe(allSubjectType, remoteExecSubjectType,
                                 new CommandResponseCallback(), null);

    }


    /**
     * Find all executors currently running. Replace existing list of executors
     * with the new one.
     *
     * @param milliseconds number of milliseconds to wait for responses from executors.
     * @throws cMsgException if not connected to cMsg server.
     */
    public void findExecutors(int milliseconds) throws cMsgException {
        if (!cmsgConnection.isConnected()) {
            throw new cMsgException("not connect to cMsg server");
        }

        executors.clear();

        // tell executors out there to respond ...
        try {
            cMsgMessage msg = new cMsgMessage();
            msg.setHistoryLengthMax(0);
            msg.setSubject(remoteExecSubjectType);
            msg.setType(allSubjectType);
            if (password != null) {
System.out.println("Sending password = " + password);
                cMsgPayloadItem item0 = new cMsgPayloadItem("p",  password);
                cMsgPayloadItem item1 = new cMsgPayloadItem("pl", passwordLength);
                msg.addPayloadItem(item0);
                msg.addPayloadItem(item1);
            }
            cMsgPayloadItem item2 = new cMsgPayloadItem("commandType", CommandType.IDENTIFY.getValue());
            cMsgPayloadItem item3 = new cMsgPayloadItem("commander", myName);
            msg.addPayloadItem(item2);
            msg.addPayloadItem(item3);
            cmsgConnection.send(msg);
        }
        catch (cMsgException e) {/*never happen*/}

        // wait for replies
        try { Thread.sleep(milliseconds); }
        catch (InterruptedException e) { }
    }


    /**
     * Get the collection of known Executors.
     * @return collection of known Executors.
     */
    public Collection<ExecutorInfo> getExecutors() {
        return executors.values();
    }


    /**
     * This class defines the callback to be run when a cMsg message
     * containing a valid command arrives.
     */
    private class CommandResponseCallback extends cMsgCallbackAdapter {

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
                        // executor process or thread telling us it's done
                        case THREAD_END:
                        case PROCESS_END:
System.out.println("Received msg --------> thread/process ended");
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
                            // Prevent any further changes to this object from the
                            // startProcess/Thread methods if the sendAndGet return
                            // of startProcess/Thread occurs after this.
                            cmdRet.lock();
                            
                            if (cmdRet != null) {
                                // Update cmdRet with the results of the process

                                // Was there any error output of the process?
                                item = msg.getPayloadItem("error");
                                if (item != null) {
                                    cmdRet.setError(item.getString());
                                }

                                // Was there any regular output of the process?
                                item = msg.getPayloadItem("output");
                                if (item != null) {
                                    cmdRet.setOutput(item.getString());
                                }

                                // Was the process/thread stopped?
                                item = msg.getPayloadItem("stopped");
                                if (item != null) {
System.out.println("send msg set to stopped, caught in callback");
                                    cmdRet.setCallbackStateIfLocked(CallbackState.STOPPED);
                                }

                                // Was the process/thread killed?
                                item = msg.getPayloadItem("killed");
                                if (item != null) {
System.out.println("send msg set to killed, caught in callback");
                                    cmdRet.setCallbackStateIfLocked(CallbackState.KILLED);
                                }

                                // Only run pending callbacks (theoretically should never be true).
                                if (cmdRet.getCallbackState() != CallbackState.PENDING) {
                                    System.out.println("Reject message, callback not pending");
                                    return;
                                }

                                cmdRet.hasTerminated(true);
                                cmdRet.executeCallback();
                            }

                            break;

                        // executor responding to "identify" command
                        case REPORTING:
                            String name = null, os = null, machine = null,
                                   release = null, processor = null, host = null;

                            // name of responding executor
                            item = msg.getPayloadItem("name");
                            if (item != null) {
                                name = item.getString();
                                if (name == null) {
                                    System.out.println("Reject message, bad format");
                                    return;
                                }
                            }

                            // host of responding executor
                            item = msg.getPayloadItem("host");
                            if (item != null) {
                                host = item.getString();
                                if (host == null) {
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
                            ExecutorInfo eInfo = new ExecutorInfo(name, host, os, machine, processor, release);
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
     * The executor will destroy all started processes and shutdown all started
     * threads before exiting.
     *
     * @param exec object specifying executor.
     * @param killProcesses <code>false</code> to leave spawned processes running,
     *                      else <code>true</code> to kill them too.
     * @throws cMsgException if no connection to cMsg server or error sending message
     */
    public void kill(ExecutorInfo exec, boolean killProcesses) throws cMsgException {
        if (!cmsgConnection.isConnected()) {
            throw new cMsgException("not connect to cMsg server");
        }

        // Remove all callbacks
        // If callback defined, commander id will be in exec's hashmap
        // since return from startThread is immediate & startProcess is .1 sec.
        // There is a race condition here since another thread may call this
        // method just before a startThread/Process actually returns. In that
        // case, the callback is not removed here since the exec does not yet
        // have the commander id entry in its hashmap. When the message to stop
        // all processes/threads reaches the exec, it will stop them resulting
        // in a message sent to this commander telling the callback to execute.
        // It is on the multithreaded user to call this method at a time when
        // all desired processes can be stopped.
        // If no cb defined, then there will be no id in exec's hashmap.
        Collection<Integer> commanderIds = exec.getCommanderIds();
        for (Integer id : commanderIds) {
            CommandReturn cmdRet = cmdReturns.remove(id);
            if (cmdRet != null) {
                cmdRet.setCallbackState(CallbackState.KILLED);
            }
        }

        // Clear out record of all commands not terminated yet
        exec.clearIds();

        // Send kill msg to executor
        cMsgMessage msg = new cMsgMessage();
        msg.setHistoryLengthMax(0);
        msg.setSubject(remoteExecSubjectType);
        msg.setType(exec.getName());
        if (password != null) {
            cMsgPayloadItem item0 = new cMsgPayloadItem("p",  password);
            cMsgPayloadItem item1 = new cMsgPayloadItem("pl", passwordLength);
            msg.addPayloadItem(item0);
            msg.addPayloadItem(item1);
        }
        cMsgPayloadItem item2 = new cMsgPayloadItem("commandType", CommandType.DIE.getValue());
        cMsgPayloadItem item3 = new cMsgPayloadItem("killProcesses", killProcesses ? 1 : 0);
        msg.addPayloadItem(item2);
        msg.addPayloadItem(item3);
        cmsgConnection.send(msg);
    }


    /**
     * Kill all executors and, optionally, all processes they have started.
     * Each executor will destroy all started processes and shutdown all started
     * threads before exiting.
     *
     * @param killProcesses <code>false</code> to leave spawned processes running,
     *                      else <code>true</code> to kill them too.
     * @throws cMsgException if no connection to cMsg server or error sending message
     */
    public void killAll(boolean killProcesses) throws cMsgException {
        if (!cmsgConnection.isConnected()) {
            throw new cMsgException("not connect to cMsg server");
        }

        for (ExecutorInfo exec : executors.values()) {
            // Set status of each callback and remove from map
            Collection<Integer> commanderIds = exec.getCommanderIds();
            for (Integer id : commanderIds) {
                CommandReturn cmdRet = cmdReturns.remove(id);
                if (cmdRet != null) {
                    cmdRet.setCallbackState(CallbackState.KILLED);
                }
            }

            // Clear out record of all commands not terminated yet
            exec.clearIds();
        }

        // Make sure we got rid of all callbacks
        cmdReturns.clear();

        // Send kill msg to all executors
        cMsgMessage msg = new cMsgMessage();
        msg.setHistoryLengthMax(0);
        msg.setSubject(remoteExecSubjectType);
        msg.setType(allSubjectType);
        if (password != null) {
            cMsgPayloadItem item0 = new cMsgPayloadItem("p",  password);
            cMsgPayloadItem item1 = new cMsgPayloadItem("pl", passwordLength);
            msg.addPayloadItem(item0);
            msg.addPayloadItem(item1);
        }
        cMsgPayloadItem item2 = new cMsgPayloadItem("commandType", CommandType.DIE.getValue());
        cMsgPayloadItem item3 = new cMsgPayloadItem("killProcesses", killProcesses ? 1 : 0);
        msg.addPayloadItem(item2);
        msg.addPayloadItem(item3);
        cmsgConnection.send(msg);
    }


    /**
     * Stop the given process or thread on the specified executor.
     * This can only be called if startProcess or startThread has
     * returned with a CommandReturn object.
     *
     * @param exec object specifying executor.
     * @param commandReturn object specifying process or thread to stop
     * @throws cMsgException if no connection to cMsg server or error sending message
     */
    public void stop(ExecutorInfo exec, CommandReturn commandReturn) throws cMsgException {
        stop(exec, commandReturn.getId());
    }

    /**
     * Stop the given process or thread on the specified executor.
     * This can only be called if startProcess or startThread has
     * returned with a CommandReturn object.
     *
     * @param exec object specifying executor.
     * @param commandId id of process or thread to stop
     * @throws cMsgException if no connection to cMsg server or error sending message
     */
    public void stop(ExecutorInfo exec, int commandId) throws cMsgException {
        if (!cmsgConnection.isConnected()) {
            throw new cMsgException("not connect to cMsg server");
        }

        // Remove any callback:
        CommandReturn cmdRet = cmdReturns.remove(commandId);
        if (cmdRet != null) {
            System.out.println("set to stopped in stop");
            cmdRet.setCallbackState(CallbackState.STOPPED);
        }

        // Retrieve executor id (id meaningful to executor) and remove from storage.
        // Since this method can only be called if startProcess or startThread has
        // returned with a CommandReturn object, exec will have a commandId key in
        // its hashmap.
        Integer execId = exec.removeCommanderId(commandId);
        if (execId == null) {
            // No such id exists for this Executor since it was already terminated
            return;
        }

        // Send stop msg to executor
        cMsgMessage msg = new cMsgMessage();
        msg.setHistoryLengthMax(0);
        msg.setSubject(remoteExecSubjectType);
        msg.setType(exec.getName());
        if (password != null) {
            cMsgPayloadItem item0 = new cMsgPayloadItem("p",  password);
            cMsgPayloadItem item1 = new cMsgPayloadItem("pl", passwordLength);
            msg.addPayloadItem(item0);
            msg.addPayloadItem(item1);
        }
        cMsgPayloadItem item2 = new cMsgPayloadItem("commandType", CommandType.STOP.getValue());
        cMsgPayloadItem item3 = new cMsgPayloadItem("id", execId);
        msg.addPayloadItem(item2);
        msg.addPayloadItem(item3);

        cmsgConnection.send(msg);
    }


    /**
     * Stop all processes and threads on the specified executor.
     *
     * @param exec object specifying executor.
     * @throws cMsgException if no connection to cMsg server or error sending message.
     */
    public void stopAll(ExecutorInfo exec) throws cMsgException {
        if (!cmsgConnection.isConnected()) {
            throw new cMsgException("not connect to cMsg server");
        }

        // Remove all callbacks.
        // If callback defined, commander id will be in exec's hashmap
        // since return from startThread is immediate & startProcess is .1 sec.
        // There is a race condition here since another thread may call this
        // method just before a startThread/Process actually returns. In that
        // case, the callback is not removed here since the exec does not yet
        // have the commander id entry in its hashmap. When the message to stop
        // all processes/threads reaches the exec, it will stop them resulting
        // in a message sent to this commander telling the callback to execute.
        // It is on the multithreaded user to call this method at a time when
        // all desired processes can be stopped.
        // If no cb defined, then there will be no id in exec's hashmap.
        Collection<Integer> commanderIds = exec.getCommanderIds();
        for (Integer id : commanderIds) {
            CommandReturn cmdRet = cmdReturns.remove(id);
            if (cmdRet != null) {
                cmdRet.setCallbackState(CallbackState.STOPPED);
            }
        }

        // Clear out record of all commands not terminated yet
        exec.clearIds();

        // Send stop-all msg to executor
        cMsgMessage msg = new cMsgMessage();
        msg.setHistoryLengthMax(0);
        msg.setSubject(remoteExecSubjectType);
        msg.setType(exec.getName());
        if (password != null) {
            cMsgPayloadItem item0 = new cMsgPayloadItem("p",  password);
            cMsgPayloadItem item1 = new cMsgPayloadItem("pl", passwordLength);
            msg.addPayloadItem(item0);
            msg.addPayloadItem(item1);
        }
        cMsgPayloadItem item2 = new cMsgPayloadItem("commandType", CommandType.STOP_ALL.getValue());
        msg.addPayloadItem(item2);
        cmsgConnection.send(msg);
    }


    /**
     * Stop all processes and threads on all executors.
     * @throws cMsgException if no connection to cMsg server or error sending message.
     */
    public void stopAll() throws cMsgException {
        if (!cmsgConnection.isConnected()) {
            throw new cMsgException("not connect to cMsg server");
        }

        for (ExecutorInfo exec : executors.values()) {
            // Set status of each callback and remove from map
            Collection<Integer> commanderIds = exec.getCommanderIds();
            for (Integer id : commanderIds) {
                CommandReturn cmdRet = cmdReturns.remove(id);
                if (cmdRet != null) {
                    cmdRet.setCallbackState(CallbackState.STOPPED);
                }
            }

            // Clear out record of all commands not terminated yet
            exec.clearIds();
        }

        // Make sure we got rid of all callbacks
        cmdReturns.clear();

        // Send stop-all msg to all executors
        cMsgMessage msg = new cMsgMessage();
        msg.setHistoryLengthMax(0);
        msg.setSubject(remoteExecSubjectType);
        msg.setType(allSubjectType);
        if (password != null) {
            cMsgPayloadItem item0 = new cMsgPayloadItem("p",  password);
            cMsgPayloadItem item1 = new cMsgPayloadItem("pl", passwordLength);
            msg.addPayloadItem(item0);
            msg.addPayloadItem(item1);
        }
        cMsgPayloadItem item2 = new cMsgPayloadItem("commandType", CommandType.STOP_ALL.getValue());
        msg.addPayloadItem(item2);
        cmsgConnection.send(msg);
    }


    /**
     * Cancel a callback previously registered by calling
     * {@link #startProcess(ExecutorInfo, String, boolean, CommandCallback , Object)}.
     *
     * @param commandReturn object associated with callback.
     */
    public void cancelCallback(CommandReturn commandReturn) {
        cmdReturns.remove(commandReturn.getId());
        commandReturn.unregisterCallback();
    }


    /**
     * Start an xterm with a process using the specified executor.
     * Do not wait for it to exit. Allows 2 seconds for return message
     * from executor before throwing exeception. Reading from xterm
     * output blocks for some reason so no monitoring of it.
     *
     * @param exec Executor to start process with.
     * @param cmd command that Executor will run inside xterm. May be null.
     * @param geometry geometry info in the form WxH+-X+-Y with W & H in chars, X & Y in pixels;
     *                 for example 75x10+0+200 or 75x10-10-20. May be null.
     * @param title window title. May be null.
     * @return object containing id number and any process output/error captured
     * @throws cMsgException if arg is null, cmsg communication fails or takes too long, or internal protocol error
     */
    public CommandReturn startXterm(ExecutorInfo exec, String cmd, String geometry, String title)
            throws cMsgException {

        // xterm with scrollbar
        String realCmd = "xterm -sb";

        // window geometry
        if (geometry != null) {
            realCmd += " -geometry " + geometry;
        }

        // window title
        if (title != null) {
            realCmd += " -T " + title;
        }

        // cmd to run in xterm
        if (cmd != null && cmd.length() > 0) {
            realCmd += " -hold -e " + cmd;
        }

        return startProcess(exec, realCmd, false, null, null);
    }


    /**
     * This method is an asynchronous means of starting an external process
     * using the specified executor.
     * The executor waits 0.1 seconds for the process to finish. If the process has terminated,
     * any output will be immediately available in the returned object if monitor is true.
     * If the process does not terminate in that time, then the returned object
     * will not contain that information yet, but will when it terminates.
     * In either case, the callback is run when the process terminates, passing userObject
     * and the updated CommandReturn object to the callback as arguments.
     * If an error starting the process occurs, it is returned immediately
     * (0.1 sec after process started), is visible in the returned object,
     * and the callback is <b>NOT</b> run. All other errors will be available
     * in the returned object as well when the process terminates.
     * All errors can be seen whether or not monitor is true.
     * Allows 2 seconds for initial return message from executor before throwing
     * an exception.
     *
     * @param exec Executor to start process with.
     * @param cmd command that Executor will run.
     * @param monitor <code>true</code> if output of the command should be captured and returned,
     *                else <code>false</code>.
     * @param callback callback to be run when process ends.
     * @param userObject object to be passed to callback as argument.
     * @return object containing id number and any process output captured
     * @throws cMsgException if arg is null, cmsg sendAndGet communication fails or takes too long,
     *                       or internal protocol error
     */
    public CommandReturn startProcess(ExecutorInfo exec, String cmd, boolean monitor,
                                      CommandCallback callback, Object userObject)
            throws cMsgException {

        try {
            return startProcess(exec, cmd, monitor, false, callback, userObject, 2000);
        }
        catch (TimeoutException e) {
            throw new cMsgException(e);
        }
    }


    /**
     * This method is a synchronous means of starting an external process
     * using the specified executor and waiting for it to finish.
     * All status information is available in the returned object.
     * All errors can be seen whether or not monitor is true.
     * If the timeout exception is thrown, the caller will no longer be
     * able to see any results from the process.
     *
     * @param exec Executor to start process with.
     * @param cmd command that Executor will run.
     * @param monitor <code>true</code> if output of the command should be captured and returned,
     *                else <code>false</code>.
     * @param timeout milliseconds to wait for reply (coming via asynchronous messaging system),
     *                0 means wait forever.
     * @return object containing id number and any process output captured
     * @throws cMsgException if arg is null, cmsg communication fails, or internal protocol error
     * @throws TimeoutException if process/thread return time exceeds timeout time.
     */
    public CommandReturn startProcess(ExecutorInfo exec, String cmd, boolean monitor, int timeout)
            throws cMsgException, TimeoutException {

        return startProcess(exec, cmd, monitor, true, null, null, timeout);
    }


    /**
     * Start an external process using the specified executor.
     * If an error starting the process occurs, it is returned immediately
     * (0.1 sec after process started), is visible in the returned object,
     * and the callback is <b>NOT</b> run. All other errors will be available
     * in the returned object as well when the process terminates.
     * All errors can be seen whether or not monitor is true.
     *
     * @param exec Executor to start process with.
     * @param cmd command that Executor will run.
     * @param monitor <code>true</code> if output of the command should be captured and returned,
     *                else <code>false</code>.
     * @param wait <code>true</code> if executor waits for the process to complete before responding,
     *             else <code>false</code>.
     * @param callback callback to be run when process ends.
     * @param userObject object to be passed to callback as argument.
     * @param timeout milliseconds to wait for reply (coming via asynchronous messaging system),
     *                0 means wait forever.
     * @return object containing id number and any process output captured
     * @throws cMsgException if arg is null, cmsg communication fails, or internal protocol error
     * @throws TimeoutException if process/thread return time exceeds timeout time.
     */
    CommandReturn startProcess(ExecutorInfo exec, String cmd, boolean monitor, boolean wait,
                               CommandCallback callback, Object userObject, int timeout)
            throws cMsgException, TimeoutException {

        if (exec == null || cmd == null) {
            throw new cMsgException("argument(s) is(are) null");
        }

        int myId = uniqueId.incrementAndGet();

        CommandReturn cmdRet = new CommandReturn(CallbackState.NONE);
        // register callback for execution on process termination
        if (callback != null) {
            cmdRet.registerCallback(callback, userObject);
            cmdRet.setCallbackState(CallbackState.PENDING);
            cmdReturns.put(myId, cmdRet);
        }

        cMsgMessage msg = new cMsgMessage();
        msg.setHistoryLengthMax(0);
        msg.setSubject(remoteExecSubjectType);
        msg.setType(exec.getName());

        try {
            if (password != null) {
                cMsgPayloadItem item0 = new cMsgPayloadItem("p",  password);
                cMsgPayloadItem item1 = new cMsgPayloadItem("pl", passwordLength);
                msg.addPayloadItem(item0);
                msg.addPayloadItem(item1);
            }
            cMsgPayloadItem item2 = new cMsgPayloadItem("commandType", CommandType.START_PROCESS.getValue());
            msg.addPayloadItem(item2);
            cMsgPayloadItem item3 = new cMsgPayloadItem("command", cmd);
            msg.addPayloadItem(item3);
            cMsgPayloadItem item4 = new cMsgPayloadItem("monitor", monitor ? 1 : 0);
            msg.addPayloadItem(item4);
            cMsgPayloadItem item5 = new cMsgPayloadItem("wait", wait ? 1 : 0);
            msg.addPayloadItem(item5);
            cMsgPayloadItem item6 = new cMsgPayloadItem("commander", myName); // cmsg "address" subject
            msg.addPayloadItem(item6);
            cMsgPayloadItem item7 = new cMsgPayloadItem("id", myId);  // send this back when process done
            msg.addPayloadItem(item7);
        }
        catch (cMsgException e) { /* no invalid names or null objects */ }

        // send msg and receive response
        cMsgMessage returnMsg = cmsgConnection.sendAndGet(msg, timeout);

        // analyze response
        if (returnMsg.hasPayload()) {
            // Was there an error?
            String err = null;
            cMsgPayloadItem item = returnMsg.getPayloadItem("error");
            if (item != null) {
                err = item.getString();
            }

            // If there was an error, was it immediate (ie Process did not run)?
            if (err != null) {
                boolean immediateError = false;
                item = returnMsg.getPayloadItem("immediateError");
                if (item != null) {
                    immediateError = item.getInt() != 0;
                }

                // If it was an immediate error, cancel
                // callback since process never ran.
                if (immediateError) {
                    if (cmdRet.getCallbackState() == CallbackState.PENDING) {
                        cmdRet.setCallbackState(CallbackState.ERROR);
                    }
                }
            }

            // Has the process already terminated?
            boolean terminated = false;
            item = returnMsg.getPayloadItem("terminated");
            if (item != null) {
                terminated = item.getInt() != 0;
            }

            // Has the process been killed or stopped?

            // If it hasn't, get its id
            int processId = 0;
            if (!terminated) {
                item = returnMsg.getPayloadItem("id");
                if (item == null) {
                    throw new cMsgException("startProcess: internal protocol error 1");
                }
                processId = item.getInt();
                // Store mapping between the 2 ids to help terminating it in future.
                exec.addCommanderId(myId, processId);
            }
            else {
                // was this process killed by call to kill() or killAll()?
                boolean killed = false;
                item = returnMsg.getPayloadItem("killed");
                if (item != null) {
                    killed = item.getInt() != 0;
System.out.println("startProcess: Executor set to killed");
                    cmdRet.setCallbackState(CallbackState.KILLED);
                }
                if (!killed) {
                    // was this process stopped by call to stop() or stopAll()?
                    item = returnMsg.getPayloadItem("stopped");
                    if (item != null) {
System.out.println("startProcess: Executor set to stopped");
                        cmdRet.setCallbackState(CallbackState.STOPPED);
                    }
                }
            }

            // If we requested the output of the process ...
            String output = null;
            if (monitor) {
                item = returnMsg.getPayloadItem("output");
                if (item != null) {
                    output = item.getString();
                }
            }

            cmdRet.setValues(myId, processId, (err != null), terminated, output, err);
            return cmdRet;
        }
        else {
            throw new cMsgException("startProcess: internal protocol error 2");
        }
    }


    /**
     * This method is an asynchronous means of starting an internal thread
     * in the specified executor and immediately returns.
     * It runs callback when the thread finishes, passing userObject and the
     * updated CommandReturn object to it as arguments.
     * Allows 2 seconds for initial return message from executor
     * before throwing exception.
     * If an error starting the thread occurs, it is returned immediately,
     * is visible in the returned object and the callback is <b>NOT</b> run.
     *
     * @param exec Executor to start thread in.
     * @param className name of java class to instantiate and run as thread in executor.
     * @param callback callback to be run when thread ends.
     * @param userObject object to be passed to callback.
     * @param constructorArgs object containing className constructor's arguments.
     *                        May be null.
     * @return object containing executor id number and any error output
     * @throws cMsgException if cmsg communication fails or takes too long,
     *                       or internal protocol error
     */
    CommandReturn startThread(ExecutorInfo exec, String className,
                              CommandCallback callback, Object userObject,
                              ConstructorInfo constructorArgs)
            throws cMsgException {

        try {
            return startThread(exec, className, false, callback, userObject, 2000, constructorArgs);
        }
        catch (TimeoutException e) {
            throw new cMsgException(e);
        }
    }


    /**
     * This method is a synchronous means of starting an internal thread
     * in the specified executor and waiting for it to finish.
     * All status information is available in the returned object.
     * If the timeout exception is thrown, the caller will no longer
     * be able to see any results from the thread.
     * If an error starting the thread occurs, it is returned immediately,
     * and is visible in the returned object.
     *
     * @param exec Executor to start thread in.
     * @param className name of java class to instantiate and run as thread in executor.
     * @param timeout milliseconds to wait for reply (coming via asynchronous messaging system),
     *                0 means wait forever.
     * @param constructorArgs object containing className constructor's arguments.
     *                        May be null.
     * @return object containing executor id number and any error output
     * @throws cMsgException if cmsg communication fails or internal protocol error
     * @throws TimeoutException if cmsg communication times out
     */
    public CommandReturn startThread(ExecutorInfo exec, String className, int timeout,
                                     ConstructorInfo constructorArgs)
            throws cMsgException, TimeoutException {

        return startThread(exec, className, true, null, null, timeout, constructorArgs);
    }

    /**
     * Starts an internal thread in the specified executor and immediately
     * returns. Allows 2 seconds for sendAndGet return message from executor
     * before throwing exception.
     * If an error starting the thread occurs, it is returned immediately,
     * is visible in the returned object and the callback is <b>NOT</b> run.
     *
     * @param exec Executor to start thread in.
     * @param className name of java class to instantiate and run as thread in executor.
     * @param wait <code>true</code> if executor waits for the process to complete before responding,
     *             else <code>false</code>.
     * @param callback callback to be run when process ends.
     * @param userObject object to be passed to callback.
     * @param timeout milliseconds to wait for reply (coming via asynchronous messaging system),
     *                0 means wait forever.
     * @param constructorArgs object containing className constructor's arguments.
     *                        May be null.
     * @return object containing executor id number and any error output
     * @throws cMsgException if args null, cmsg communication fails, or internal protocol error
     * @throws TimeoutException if cmsg communication times out
     */
    CommandReturn startThread(ExecutorInfo exec, String className, boolean wait,
                              CommandCallback callback, Object userObject, int timeout,
                              ConstructorInfo constructorArgs)
            throws cMsgException, TimeoutException {

        if (exec == null || className == null) {
            throw new cMsgException("argument(s) is(are) null");
        }

        int myId = uniqueId.incrementAndGet();

        CommandReturn cmdRet = new CommandReturn(CallbackState.NONE);
        // register callback for execution on process termination
        if (callback != null) {
            cmdRet.registerCallback(callback, userObject);
            cmdRet.setCallbackState(CallbackState.PENDING);
            cmdReturns.put(myId, cmdRet);
        }

        cMsgMessage msg = new cMsgMessage();
        msg.setHistoryLengthMax(0);
        msg.setSubject(remoteExecSubjectType);
        msg.setType(exec.getName());

        try {
            if (password != null) {
                cMsgPayloadItem item0 = new cMsgPayloadItem("p",  password);
                cMsgPayloadItem item1 = new cMsgPayloadItem("pl", passwordLength);
                msg.addPayloadItem(item0);
                msg.addPayloadItem(item1);
            }
            cMsgPayloadItem item2 = new cMsgPayloadItem("commandType", CommandType.START_THREAD.getValue());
            msg.addPayloadItem(item2);
            cMsgPayloadItem item3 = new cMsgPayloadItem("className", className);
            msg.addPayloadItem(item3);
            cMsgPayloadItem item4 = new cMsgPayloadItem("wait", wait ? 1 : 0);
            msg.addPayloadItem(item4);
            cMsgPayloadItem item5 = new cMsgPayloadItem("commander", myName); // cmsg "address" subject
            msg.addPayloadItem(item5);
            cMsgPayloadItem item6 = new cMsgPayloadItem("id", myId);  // send this back when process done
            msg.addPayloadItem(item6);
            if (constructorArgs != null) {
                // msg contains constructor args
                cMsgPayloadItem item7 = new cMsgPayloadItem("args", constructorArgs.createMessageFromArgs());
                msg.addPayloadItem(item7);
            }
        }
        catch (cMsgException e) { /* no invalid names or null objects */ }

        // send msg and receive response
        cMsgMessage returnMsg = cmsgConnection.sendAndGet(msg, timeout);

        // analyze response
        if (returnMsg.hasPayload()) {
            // Was there an error? Unlike a process, if a thread fails,
            // it fails to start (i.e. immediately) and is terminated.
            cMsgPayloadItem item = msg.getPayloadItem("error");
            if (item != null) {
                String err = item.getString();
                if (cmdRet.getCallbackState() == CallbackState.PENDING) {
                    cmdRet.setCallbackState(CallbackState.ERROR);
                }
                cmdRet.setValues(myId, 0, true, true, null, err);
                return cmdRet;
            }

            // Has the thread terminated? (i.e. did we wait for it?)
            boolean terminated = false;
            item = returnMsg.getPayloadItem("terminated");
            if (item != null) {
                terminated = item.getInt() != 0;
            }

            // If it hasn't, get its id
            int threadId = 0;
            if (!terminated) {
                item = returnMsg.getPayloadItem("id");
                if (item == null) {
                    throw new cMsgException("startThread: internal protocol error");
                }
                threadId = item.getInt();
                // Store mapping between the 2 ids to help terminating it in future.
                exec.addCommanderId(myId, threadId);
            }
            else {
                // was this thread killed by call to kill() or killAll()?
                boolean killed = false;
                item = returnMsg.getPayloadItem("killed");
                if (item != null) {
                    killed = item.getInt() != 0;
                    cmdRet.setCallbackState(CallbackState.KILLED);
                }
                if (!killed) {
                    // was this thread stopped by call to stop() or stopAll()?
                    item = returnMsg.getPayloadItem("stopped");
                    if (item != null) {
                        cmdRet.setCallbackState(CallbackState.STOPPED);
                    }
                }
            }

            cmdRet.setValues(myId, threadId, false, false, null, null);
            return cmdRet;
        }
        else {
            throw new cMsgException("startThread: internal protocol error");
        }
    }


    /**
     * Method to get a line of keyboard input.
     * @param s prompt to display
     * @return line of keyboard input
     */
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
        String[] stuff = new String[3];

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
            else if (args[i].equalsIgnoreCase("-p")) {
                stuff[2]= args[i + 1];   // password
                i++;
            }
        }

        return stuff;
    }


    /**
     * This method calculates the geometry specification of a given number of
     * identical xterm windows on the local display screen in a conveniently
     * packed manner.
     * The output is list of strings in the format WxH+X+Y, which can be used
     * directly in the command to start up an xterm.
     *
     * @param count number of xterms.
     * @param widthChars width of each xterm in characters.
     * @param heightChars number of lines in each xterm.
     * @return list of strings in WxH+X+Y format specifying geometry of each xterm
     *         to be displayed.
     */
    static public List<String> xtermGeometry(int count, int widthChars, int heightChars) {

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
        String geoSize = widthChars+"x"+heightChars;
        ArrayList<String> geoList = new ArrayList<String>(count);

        while (stillRoomY || stillRoomX) {
            geo = geoSize + "+" + startX + "+" + startY;
            geoList.add(geo);
            startY += xtermHeight;

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

        return geoList;
    }

    /**
     * This method is example of how to make a bunch of identical xterm windows
     * fill the screen in a conveniently packed manner.
     *
     * @param executors list of Executors to use.
     * @param command command to run in each executor.
     * @param widthChars width of each xterm in characters.
     * @param heightChars number of lines in each xterm.
     *
     * @return list of CommandReturn objects.
     *
     * @throws cMsgException
     */
    public List<CommandReturn> startCmdInWindows(List<ExecutorInfo> executors,
                                                  String command,
                                                  int widthChars, int heightChars)
            throws cMsgException {

        List<String> geometries = xtermGeometry(executors.size(), widthChars, heightChars);
        ArrayList<CommandReturn> returnList = new ArrayList<CommandReturn>(executors.size());

        for (int i=0; i <executors.size(); i++) {
            // create xterm and add return object to list
            returnList.add(startXterm(executors.get(i), command,
                                      geometries.get(i), executors.get(i).getName()));
        }

        return returnList;
    }


    /**
     * This method is example of how to make a bunch of identical xterm windows
     * fill the screen in a conveniently packed manner.
     *
     * @param exec Executor to use.
     * @param count number of windows to make.
     * @param widthChars width of each xterm in characters.
     * @param heightChars number of lines in each xterm.
     *
     * @return list of CommandReturn objects.
     *
     * @throws cMsgException
     */
    public List<CommandReturn> startWindows(ExecutorInfo exec, String title,
                                            int count, int widthChars, int heightChars)
            throws cMsgException {

        List<String> geometries = xtermGeometry(count, widthChars, heightChars);
        ArrayList<CommandReturn> returnList = new ArrayList<CommandReturn>(geometries.size());
        for (String geo : geometries) {
            // create xterm and add return object to list
            returnList.add(startXterm(exec, null, geo, title));
        }

        return returnList;
    }



    public static void main0(String[] args) {

        try {
            String[] arggs = decodeCommandLine(args);
System.out.println("Starting Executor with:\n  name = " + arggs[1] + "\n  udl = " + arggs[0]);
            Commander cmdr = new Commander(arggs[0], arggs[1], "commander", arggs[2]);
            Collection<ExecutorInfo> execList = cmdr.getExecutors();
            for (ExecutorInfo info : execList) {
                System.out.println("Found executor: name = " + info.getName() + " running on " + info.getOS());
            }


            if (execList.size() > 0) {
                List<CommandReturn> retList = cmdr.startCmdInWindows(new ArrayList<ExecutorInfo>(execList),
                                                                     "who", 85, 8);
            }

            while(true) {
                try {Thread.sleep(1000);}
                catch (InterruptedException e) {}
            }
        }
        catch (cMsgException e) {
            e.printStackTrace();
            System.exit(-1);
        }
    }

    public static void main1(String[] args) {

        try {
            String[] arggs = decodeCommandLine(args);
 System.out.println("Starting Executor with:\n  name = " + arggs[1] + "\n  udl = " + arggs[0]);
            Commander cmdr = new Commander(arggs[0], arggs[1], "commander", arggs[2]);
            Collection<ExecutorInfo> execList = cmdr.getExecutors();
            for (ExecutorInfo info : execList) {
                System.out.println("Found executor: name = " + info.getName() + " running on " + info.getOS());
            }

            if (execList.size() > 0) {
                ArrayList<ExecutorInfo> list = new ArrayList<ExecutorInfo>(execList);
                List<CommandReturn> retList = cmdr.startWindows(list.get(0),
                                                                list.get(0).getName(),
                                                                20, 85, 8);
                for (CommandReturn ret : retList) {
                    cmdr.stop(list.get(0), ret);
                    try {Thread.sleep(200);}
                    catch (InterruptedException e) {}
                }
            }
        }
        catch (cMsgException e) {
            e.printStackTrace();
            System.exit(-1);
        }
    }

    /**
     * Run as a stand-alone application
     */
    public static void main2(String[] args) {
        try {
            String[] arggs = decodeCommandLine(args);
System.out.println("Starting Executor with:\n  name = " + arggs[1] + "\n  udl = " + arggs[0]);
            Commander cmdr = new Commander(arggs[0], arggs[1], "commander", arggs[2]);
            Collection<ExecutorInfo> execList = cmdr.getExecutors();
            System.out.println("execList =  "+ execList);
            for (ExecutorInfo info : execList) {
                System.out.println("Found executor: name = " + info.getName() + " running on " + info.getOS());
            }

            class myCB implements CommandCallback {
                public void callback(Object userObject, CommandReturn commandReturn) {
                    System.out.println("In callback, process output = \n" + commandReturn.getOutput());
                    System.out.println("               error output = \n" + commandReturn.getError());
                }
            }

                if (execList.size() > 0) {

                    ConstructorInfo exThrCon = new ConstructorInfo();
                    ConstructorInfo recCon = new ConstructorInfo();
                    ConstructorInfo dimCon = new ConstructorInfo();

                    exThrCon.addReferenceArg("java.awt.Rectangle", recCon);
                    recCon.addReferenceArg("java.awt.Dimension", dimCon);
                    dimCon.addPrimitiveArg("int", "1");
                    dimCon.addPrimitiveArg("int", "2");

                    ArrayList<ExecutorInfo> list = new ArrayList<ExecutorInfo>(execList);
                    CommandReturn ret = cmdr.startThread(list.get(0),
                                                         "org.jlab.coda.cMsg.remoteExec.ExampleThread",
                                                         new myCB(), null, exThrCon);
System.out.println("Return = " + ret);
                    if (ret.hasError()) {
                        System.out.println("@@@@@@@ ERROR @@@@@@@:\n" + ret.getError());
                    }
                    if (ret.getOutput() != null) {
                        System.out.println("Regular output:\n" + ret.getOutput());
                    }

                    //while(true) {
                        try {Thread.sleep(10000);}
                        catch (InterruptedException e) {}
                    //}

                    //cmdr.stopAll(execList.get(0));
                    cmdr.stop(list.get(0), ret);

                }

        }
//        catch (TimeoutException e) {
//            e.printStackTrace();
//            System.exit(-1);
//        }
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
            Commander cmdr = new Commander(arggs[0], arggs[1], "commander", arggs[2]);
            Collection<ExecutorInfo> execList = cmdr.getExecutors();
            System.out.println("execList =  "+ execList);
            for (ExecutorInfo info : execList) {
                System.out.println("Found executor: name = " + info.getName() + " running on " + info.getOS());
            }

            class myCB implements CommandCallback {
                public void callback(Object userObject, CommandReturn commandReturn) {
                    System.out.println("In callback, process output = \n" + commandReturn.getOutput());
                    System.out.println("               error output = \n" + commandReturn.getError());
                }
            }

            if (execList.size() > 0) {
                // try starting up cMsg server ...
                ConstructorInfo serverCon = new ConstructorInfo();

                serverCon.addPrimitiveArg("int", ""+47000);  // port
                serverCon.addPrimitiveArg("int", ""+47001);  // domain port
                serverCon.addPrimitiveArg("int", ""+47000);  // udp port
                serverCon.addPrimitiveArg("boolean", "true"); // stand alone
                serverCon.addPrimitiveArg("boolean", "true"); // monitoring off
                serverCon.addReferenceArg("java.lang.String", null);   // client password
                serverCon.addReferenceArg("java.lang.String", null);   // cloud password
                serverCon.addReferenceArg("java.lang.String", null);   // server to join in cloud
                serverCon.addPrimitiveArg("int", ""+cMsgConstants.debugInfo); // debug level
                serverCon.addPrimitiveArg("int", ""+cMsgConstants.regimeLowMaxClients); // max clients/domain server in low regime

                ArrayList<ExecutorInfo> list = new ArrayList<ExecutorInfo>(execList);
                CommandReturn ret = cmdr.startThread(list.get(0),
                                                     "org.jlab.coda.cMsg.cMsgDomain.server.cMsgNameServer",
                                                     new myCB(), null, serverCon);
                System.out.println("Return = " + ret);
                if (ret.hasError()) {
                    System.out.println("@@@@@@@ ERROR @@@@@@@:\n" + ret.getError());
                }
                if (ret.getOutput() != null) {
                    System.out.println("Regular output:\n" + ret.getOutput());
                }

                //while(true) {
                try {Thread.sleep(10000);}
                catch (InterruptedException e) {}
                //}

                cmdr.stop(list.get(0), ret);
            }
        }
        catch (cMsgException e) {
            e.printStackTrace();
            System.exit(-1);
        }
    }

    /**
     * Run as a stand-alone application
     */
    public static void main(String[] args) {
        try {
            String[] arggs = decodeCommandLine(args);
System.out.println("Starting Executor with:\n  name = " + arggs[1] + "\n  udl = " + arggs[0]);
            Commander cmdr = new Commander(arggs[0], arggs[1], "commander", arggs[2]);
            //cmdr.findExecutors();   // already done in constructor
            Collection<ExecutorInfo> execList = cmdr.getExecutors();
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

//            class myCB implements CommandCallback {
//                public void callback(Object userObject, CommandReturn commandReturn) {
//                    System.out.println("In callback, process output = \n" + commandReturn.getOutput());
//                    System.out.println("               error output = \n" + commandReturn.getError());
//                }
//            }

            String in;
            while(true) {
                in = inputStr("% ");
                if (execList.size() > 0) {
                    ArrayList<ExecutorInfo> list = new ArrayList<ExecutorInfo>(execList);
                    //                                                      monitor, wait
                    //CommandReturn ret = cmdr.startProcess(list.get(0), in, true, true, new myCB(), null, 10000);
                    //CommandReturn ret = cmdr.startProcess(list.get(0), in, true,  new myCB(), null);
                    CommandReturn ret = cmdr.startProcess(list.get(0), in, true,  12000);
                    System.out.println("Synchronous return:");
                    System.out.println("  Callback state = " + ret.getCallbackState());
                    if (ret.hasError()) {
                        System.out.println("  Error = " + ret.getError());
                    }
                    if (ret.hasTerminated()) {
                        System.out.println("  Process = TERMINATED");
                    }
                    if (ret.getOutput() != null) {
                        System.out.println("  Output = --");
                        System.out.println("<------------");
                        System.out.println(ret.getOutput());
                    }

//                    try {Thread.sleep(1000);}
//                    catch (InterruptedException e) {}
//
//                    System.out.println("Stop process now");
//                    cmdr.stop(list.get(0), ret.getId());

//                    while (true) {
                        try {Thread.sleep(1000);}
                        catch (InterruptedException e) {}
                    System.out.println("After 1 sec --> Callback state = " + ret.getCallbackState());

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

    /**
      * Run as a stand-alone application
      */
     public static void main5(String[] args) {
             String[] arggs = decodeCommandLine(args);
 System.out.println("Starting Executor with:\n  name = " + arggs[1] + "\n  udl = " + arggs[0]);
        Commander cmdr = null;
        try {
            cmdr = new Commander(arggs[0], arggs[1], "commander", arggs[2]);
        }
        catch (cMsgException e) {
            e.printStackTrace();
            return;
        }

        Collection<ExecutorInfo> execList = cmdr.getExecutors();
             System.out.println("execList =  "+ execList);
             for (ExecutorInfo info : execList) {
                 System.out.println("Found executor: name = " + info.getName() + " running on " + info.getOS());
             }

             class myCB implements CommandCallback {
                 public void callback(Object userObject, CommandReturn commandReturn) {
                     System.out.println(" %%%%%%%%%%% Ran callback %%%%%%%%%%");
                     System.out.println("               error   output = \n" + commandReturn.getError());
                     System.out.println("               regular output = \n" + commandReturn.getOutput());
                 }
             }

             String in;
             while(true) {
                 in = inputStr("% ");
//                 try {
//                     cmdr.findExecutors(500);
//                 }
//                 catch (cMsgException e) {
//                     e.printStackTrace();  //To change body of catch statement use File | Settings | File Templates.
//                 }
                 for (ExecutorInfo exec : execList) {
                     //                                    exec, cmd, monitor,     cb,  cb arg
                     try {
                         CommandReturn ret = cmdr.startProcess(exec, in, true,  new myCB(), null);
                         System.out.println("Return = " + ret);
                         if (ret.hasError()) {
                             System.out.println("@@@@@@@ ERROR @@@@@@@:\n" + ret.getError());
                         }
                         if (ret.getOutput() != null) {
                             System.out.println("Regular output:\n" + ret.getOutput());
                         }
                         System.out.println("Callback state = " + ret.getCallbackState());

                         try {Thread.sleep(1000);}
                         catch (InterruptedException e) {}

//                         System.out.println("Stop process now");
//                         cmdr.stop(exec, ret);
//                         System.out.println("Kill process now");
//                         cmdr.kill(exec, true);

//                    while (true) {
                         try {Thread.sleep(1000);}
                         catch (InterruptedException e) {}
                         System.out.println("After 1 sec --> Callback state = " + ret.getCallbackState());

//                    }
                     }
                     catch (cMsgException e) {
                         e.printStackTrace();
                     }
                 }
             }

     }


}
