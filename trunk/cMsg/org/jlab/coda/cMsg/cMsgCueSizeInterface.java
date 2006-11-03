package org.jlab.coda.cMsg;

/**
 * Interface used in the cMsgCallbackInterface and adapter to allow
 * callback objects to access cue sizes in the thread which calls them.
 */
public interface cMsgCueSizeInterface {
    /**
     * Method to get the number of messages in the cue for a callback.
     * @return number of messages in the cue for a callback
     */
    public int getCueSize();

}
