package org.jlab.coda.cMsg;

/**
 * The class defines the default context that a message starts with when it's created.
 */
public class cMsgMessageContextDefault implements cMsgMessageContext {

    boolean reliableSend = true;

    public cMsgMessageContextDefault() {
    }

    public cMsgMessageContextDefault(boolean reliableSend) {
        this.reliableSend = reliableSend;
    }
    
    /**
     * Gets the domain this callback is running in.
     * @return domain this callback is running in.
     */
    public String getDomain() {return null;}


    /**
     * Gets the subject of this callback's subscription.
     * @return subject of this callback's subscription.
     */
    public String getSubject() {return null;}


    /**
     * Gets the type of this callback's subscription.
     * @return type of this callback's subscription.
     */
    public String getType() {return null;}


    /**
     * Gets the value of this callback's cue size.
     * @return value of this callback's cue size, -1 if no info available
     */
    public int getCueSize() {return -1;}


    /**
     * Sets whether the send will be reliable (default, TCP)
     * or will be allowed to be unreliable (UDP).
     *
     * @param b false if using UDP, or true if using TCP
     */
    public void setReliableSend(boolean b) {
        reliableSend = b;
    }


    /**
     * Gets whether the send will be reliable (default, TCP)
     * or will be allowed to be unreliable (UDP).
     *
     * @return value true if using TCP, else false
     */
    public boolean getReliableSend() {
        return reliableSend;
    }

}
