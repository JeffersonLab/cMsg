package org.jlab.coda.cMsg.remoteExec;

/**
 * Enumerate the possible states of a callback.
 *
 * @author timmer
 * Date: Nov 2, 2010
 */
public enum CallbackState {

    /** No callback exists. */
    NONE      ("none"),
    /** Callback was already run. */
    RUN       ("run"),
    /** Callback cancelled because error getting process or thread to run. */
    ERROR     ("error"),
    /** Callback is scheduled to be run. */
    PENDING   ("pending"),
    /** Callback was cancelled by killing executor with calls to
     * {@link Commander#kill  or {@link Commander#killAll(boolean)}. */
    KILLED   ("killed"),
    /** Callback was cancelled by stopping executor process or thread with
     *  calls to {@link Commander#stop}  or {@link Commander#stopAll}. */
    STOPPED   ("stopped"),
    /** Callback was cancelled by calling {@link Commander#cancelCallback(CommandReturn)}. */
    CANCELLED ("cancelled");

    private String value;

    private CallbackState(String value) {
        this.value = value;
    }

    /**
     * Get the enum's value.
     *
     * @return the value, e.g., "reporting" for an REPORTING.
     */
    public String getValue() {
        return value;
    }

    /**
     * Obtain the name from the value.
     *
     * @param value the value to match.
     * @return the name, or <code>null</code>.
     */
    public static String getName(String value) {
        CallbackState states[] = CallbackState.values();
        for (CallbackState state : states) {
            if (state.value.equalsIgnoreCase(value)) {
                return state.name();
            }
        }

        return null;
    }

    /**
     * Obtain the enum from the value.
     *
     * @param value the value to match.
     * @return the matching enum, or <code>null</code>.
     */
    public static CallbackState getCallbackState(String value) {
        CallbackState states[] = CallbackState.values();
        for (CallbackState state : states) {
            if (state.value.equalsIgnoreCase(value)) {
                return state;
            }
        }

        return null;
    }

}
