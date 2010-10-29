package org.jlab.coda.cMsg.remoteExec;

/**
 * This enumerates the type of information that can be sent from Executor to Commander.
 *
 * @author timmer
 * Date: Oct 12, 2010
 */
public enum InfoType {

    REPORTING   ("reporting"),   // general Executor information
    THREAD_END  ("thread_end"),  // notification of ended thread
    PROCESS_END ("process_end"); // notification of ended process

    private String value;

    private InfoType(String value) {
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
    public static InfoType getInfoType(String value) {
        InfoType infoTypes[] = InfoType.values();
        for (InfoType dt : infoTypes) {
            if (dt.value.equalsIgnoreCase(value)) {
                return dt;
            }
        }

        return null;
    }


}
