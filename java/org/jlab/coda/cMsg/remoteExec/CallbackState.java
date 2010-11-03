package org.jlab.coda.cMsg.remoteExec;

/**
 * Enumerate the possible states of a callback.
 *
 * @author timmer
 * Date: Nov 2, 2010
 */
public enum CallbackState {

    NONE      ("none"),      // no callback
    RUN       ("run"),       // callback already run
    PENDING   ("pending"),   // callback scheduled to be run
    CANCELLED ("cancelled"); // callback was cancelled (not run)

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
