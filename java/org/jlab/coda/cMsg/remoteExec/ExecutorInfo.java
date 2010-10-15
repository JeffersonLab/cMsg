package org.jlab.coda.cMsg.remoteExec;

import java.util.HashMap;


/**
 * @author timmer
 * Date: Oct 13, 2010
 */
public class ExecutorInfo {

    private String name;
    private String os;
    private String machine;
    private String processor;
    private String release;

    // list of processes/threads started
    private HashMap<Integer, Integer> processAndThreadMap = new HashMap<Integer, Integer>(30);

    public ExecutorInfo(String name, String os, String machine, String processor,String release) {
        this.name = name;
        this.os = os;
        this.machine = machine;
        this.release = release;
        this.processor = processor;
    }

    public String getName() {
        return name;
    }

    public String getHost() {
        return name;
    }

    public String getOS() {
        return os;
    }

    public String getMachine() {
        return machine;
    }

    public String getProcessor() {
        return processor;
    }

    public String getOSRelease() {
        return release;
    }

    public void addCommanderId(int commanderId, int executorId) {
        // do not allow duplicate entries
        if (!processAndThreadMap.containsKey(commanderId)) {
             processAndThreadMap.put(commanderId, executorId);
        }
    }

    public Integer removeCommanderId(int commanderId) {
        return processAndThreadMap.remove(commanderId);
    }

    public Integer getExecutorId(int commanderId) {
        return processAndThreadMap.get(commanderId);
    }

    public void clearIds() {
        processAndThreadMap.clear();
    }

}
