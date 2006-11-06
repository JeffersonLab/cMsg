package org.jlab.coda.cMsg;

/**
 * Interface defining the form of the last argument to a subscription callback.
 */
public interface cMsgCallbackArgument {
    /**
     * Gets the value of the callback cue size.
     * @return value of the callback cue size
     */
    public int getCueSize();

}
