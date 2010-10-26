package org.jlab.coda.cMsg.remoteExec;

/**
 * @author timmer
 * Date: Oct 26, 2010
 */
public enum ArgType {

    STRING   (1), // Object being constructed takes String as arg to constructor
    NOARG    (2), // Object being constructed uses no-arg constructor
    MESSAGE  (3), // Object being constructed takes a custom number and type of args to constructor
    NULL     (4); // Argument is null (nothing to construct)

    private int value;

    private ArgType(int value) {
        this.value = value;
    }

    /**
     * Get the enum's value.
     *
     * @return the value, e.g., 1 for STRING.
     */
    public int getValue() {
        return value;
    }

    /**
     * Obtain the name from the value.
     *
     * @param value the value to match.
     * @return the name, or <code>null</code>.
     */
    public static String getName(int value) {
        ArgType argTypes[] = ArgType.values();
        for (ArgType dt : argTypes) {
            if (dt.value == value) {
                return dt.name();
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
    public static ArgType getArgType(int value) {
        ArgType argTypes[] = ArgType.values();
        for (ArgType dt : argTypes) {
            if (dt.value == value) {
                return dt;
            }
        }

        return null;
    }



}
