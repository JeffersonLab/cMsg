package org.jlab.coda.cMsg.remoteExec;

/**
 * Information that can be sent from Executor to controlling cMsg app.
 *
 * @author timmer
 * Date: Oct 12, 2010
 */
public enum InfoType {

    SEARCHING  ("searching"),  // searching for cmsg server to connect to
    REPORTING  ("reporting"),  // send general Executor information
    RESULT     ("result"),     // send back result of executed command
    ERROR      ("error");      // send back error resulting from attempted command execution

    private String value;

    private InfoType(String value) {
        this.value = value;
    }

    /**
     * Get the enum's value.
     *
     * @return the value, e.g., "error" for an ERROR.
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
        InfoType infoTypes[] = InfoType.values();
        for (InfoType dt : infoTypes) {
            if (dt.value.equalsIgnoreCase(value)) {
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
    public static InfoType getCommandType(String value) {
        InfoType infoTypes[] = InfoType.values();
        for (InfoType dt : infoTypes) {
            if (dt.value.equalsIgnoreCase(value)) {
                return dt;
            }
        }

        return null;
    }


}
