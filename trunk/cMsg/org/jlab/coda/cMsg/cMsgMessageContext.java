package org.jlab.coda.cMsg;

/**
 * Interface defining the context in which a subscription's callback is run.
 */
public interface cMsgMessageContext {

    /**
     * Gets the domain this callback is running in.
     * @return domain this callback is running in.
     */
    public String getDomain();


    /**
     * Gets the subject of this callback's subscription.
     * @return subject of this callback's subscription.
     */
    public String getSubject();


    /**
     * Gets the type of this callback's subscription.
     * @return type of this callback's subscription.
     */
    public String getType();


    /**
     * Gets the value of this callback's cue size.
     * @return value of this callback's cue size, -1 if no info available
     */
    public int getCueSize();


    /**
     * Sets whether the send will be reliable (default, TCP)
     * or will be allowed to be unreliable (UDP).
     *
     * @param b false if using UDP, or true if using TCP
     */
    public void setReliableSend(boolean b);


    /**
     * Gets whether the send will be reliable (default, TCP)
     * or will be allowed to be unreliable (UDP).
     *
     * @return value true if using TCP, else false
     */
    public boolean getReliableSend();

}
