package org.jlab.coda.cMsg.remoteExec;

import java.util.Collection;
import java.util.concurrent.ConcurrentHashMap;


/**
 * This class contains information representative of a single Executor
 * and is used by a Commander.
 *
 * @author timmer
 * Date: Oct 13, 2010
 */
public class ExecutorInfo {

    /** Executor name. */
    private String name;
    /** Host Executor running on. */
    private String host;
    /** Os Executor running on. */
    private String os;
    /** Machine type Executor running on. */
    private String machine;
    /** Processor type Executor running on. */
    private String processor;
    /** Os release Executor running on. */
    private String release;

    /**
     * Map of executor process/thread id (value) indexed to commander id (key).
     * Commander id used to look up executor id when process or thread needs to
     * be stopped.
     */
    private ConcurrentHashMap<Integer, Integer> processAndThreadMap = new ConcurrentHashMap<Integer, Integer>(30);


    /**
     * Constructor.
     *
     * @param name Executor name.
     * @param host host Executor running on.
     * @param os os Executor running on.
     * @param machine machine type Executor running on.
     * @param processor processor type Executor running on.
     * @param release os release Executor running on.
     */
    public ExecutorInfo(String name, String host, String os,
                        String machine, String processor, String release) {
        this.name = name;
        this.host = host;
        this.os = os;
        this.machine = machine;
        this.release = release;
        this.processor = processor;
    }

    /**
     * Get the name of the Executor.
     * @return name of the Executor.
     */
    public String getName() {
        return name;
    }

    /**
     * Get the name of the host the Executor is running on.
     * @return name of the host the Executor is running on.
     */
    public String getHost() {
        return host;
    }

    /**
     * Get the operating system the Executor is running on.
     * @return operating system the Executor is running on.
     */
    public String getOS() {
        return os;
    }

    /**
     * Get the machine type the Executor is running on.
     * @return machine type the Executor is running on.
     */
    public String getMachine() {
        return machine;
    }

    /**
     * Get the processor the Executor is running on.
     * @return machine processor the Executor is running on.
     */
    public String getProcessor() {
        return processor;
    }

    /**
     * Get the operating system release the Executor is running on.
     * @return operating system release the Executor is running on.
     */
    public String getOSRelease() {
        return release;
    }

    /**
     * Store a commander id, executor id pair in Map, both of which
     * correspond to the same start-process or start-thread action.
     *
     * @param commanderId commander's id for start process or start thread action
     * @param executorId  executor's id for same action
     */
    void addCommanderId(int commanderId, int executorId) {
        // do not allow duplicate entries
        if (processAndThreadMap.containsKey(commanderId)) {
            System.out.println("addCommanderId: already have commanderId = " + commanderId + " in hashmap");
        }
        processAndThreadMap.put(commanderId, executorId);
    }

    /**
     * Remove a commander id / executor id pair from Map.
     * 
     * @param commanderId commander id to remove from Map.
     * @return executor id paired with the given commander id.
     */
    Integer removeCommanderId(int commanderId) {
        return processAndThreadMap.remove(commanderId);
    }

    /**
     * Get the executor id corresponding to the given commander id.
     *
     * @param commanderId commander id
     * @return executor id corresponding to the given commander id.
     */
    Integer getExecutorId(int commanderId) {
        return processAndThreadMap.get(commanderId);
    }

    /**
     * Get all the commander ids.
     *
     * @return array of all the commander ids.
     */
    Collection<Integer> getCommanderIds() {
        return processAndThreadMap.keySet();
    }

    /**
     * Clear everything in the Map.
     */
    void clearIds() {
        processAndThreadMap.clear();
    }

}
