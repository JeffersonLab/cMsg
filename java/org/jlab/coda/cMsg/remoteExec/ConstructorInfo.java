package org.jlab.coda.cMsg.remoteExec;

import org.jlab.coda.cMsg.cMsgException;
import org.jlab.coda.cMsg.cMsgMessage;
import org.jlab.coda.cMsg.cMsgPayloadItem;

import java.util.LinkedList;

/**
 * This class stores all arguments needed to construct an object.
 * This information is stored in a cMsgMessage object which is a
 * useful format when sending data using cMsg.
 *
 * @author timmer
 * Date: Oct 28, 2010
 */
public class ConstructorInfo {

    /** Number of primitive type arguments. */
    int numPrimitiveArgs;

    /**
     * Number of reference type arguments that are
     * not null or do not use a no-arg constructor.
     */
    int numReferenceArgs;

    /** List of arguments. */
    LinkedList<ConstructorArg> argList = new LinkedList<ConstructorArg>();

    /** Convenience class to store data. */
    private class ConstructorArg {
        boolean isPrimitive;
        String className;
        ArgType type;
        String strValue;
        char charValue;
        ConstructorInfo info;
    }

    /** Constructor. */
    public ConstructorInfo() {
    }

    /**
     * Clear all arguments.
     */
    synchronized public void clearArgs() {
        argList.clear();
    }

    /**
     * Is the given name a valid primitive type?
     *
     * @param name type name
     * @return <code>true</code> if valid primitive type, else <code>false</code>
     */
    private boolean isPrimitive(String name) {
        if (name == null) return false;

        if (    name.equals("int")     ||
                name.equals("boolean") ||
                name.equals("char")    ||
                name.equals("byte")    ||
                name.equals("short")   ||
                name.equals("long")    ||
                name.equals("float")   ||
                name.equals("double") )  {
            return true;
        }
        return false;
    }

    //--------------------------------------------------------------------------
    // PRIMITIVES
    //--------------------------------------------------------------------------
    /**
     * Store data about primitive constructor argument.
     *
     * @param name name of primitive type
     * @param value value of argument
     * @return object containing argument data
     */
    private ConstructorArg createPrimitiveArg(String name, String value) {
        ConstructorArg arg = new ConstructorArg();
        arg.isPrimitive = true;
        arg.className = name;
        arg.type = ArgType.PRIMITIVE;
        arg.strValue = value;
        numPrimitiveArgs++;
        return arg;
    }

    /**
     * Add to the argument list a primitive type (except char) with the given value.
     * Valid names are: int, boolean, char, byte, short, long, float, double.
     *
     * @param name name of primitive type
     * @param value value of the argument in String form
     * @throws cMsgException
     */
    synchronized public void addPrimitiveArg(String name, String value) throws cMsgException {
        if (!isPrimitive(name) || name.equals("char")) {
            throw new cMsgException("name must correspond to primitive type (not char)");
        }
        argList.add(createPrimitiveArg(name, value));
    }

    /**
     * Add to the argument list a primitive type (except char) with the given value
     * at the given index into the existing list of arguments.
     * Valid names are: int, boolean, char, byte, short, long, float, double.
     *
     * @param name name of primitive type
     * @param value value of the argument in String form
     * @param index index into the existing argument list at which to add this arg
     * @throws cMsgException
     */
    synchronized public void addPrimitiveArg(String name, String value, int index) throws cMsgException {
        if (!isPrimitive(name) || name.equals("char")) {
            throw new cMsgException("class must correspond to primitive type (not char)");
        }

        // avoid throwing exception
        if (index > argList.size()) {
            argList.addLast(createPrimitiveArg(name, value));
        }
        else {
            argList.add(index, createPrimitiveArg(name, value));
        }
    }

    //--------------------------------------------------------------------------
    // PRIMITIVE CHAR
    //--------------------------------------------------------------------------
    private ConstructorArg createPrimitiveCharArg(String className, char value) {
        ConstructorArg arg = new ConstructorArg();
        arg.isPrimitive = true;
        arg.className = className;
        arg.type = ArgType.PRIMITIVE;
        arg.charValue = value;
        numPrimitiveArgs++;
        return arg;
    }

    // construct primitive char type object with given value
    synchronized public void addPrimitiveArg(String className, char value) throws cMsgException {
        if (className.equals("char")) {
            throw new cMsgException("class must correspond to primitive type char only");
        }
        argList.add(createPrimitiveCharArg(className, value));
    }

    // construct primitive char type object with given value
    synchronized public void addPrimitiveArg(String className, char value, int index) throws cMsgException {
        if (className.equals("char")) {
            throw new cMsgException("class must correspond to primitive type char only");
        }

        if (index > argList.size()) {
            argList.addLast(createPrimitiveCharArg(className, value));
        }
        else {
            argList.add(index, createPrimitiveCharArg(className, value));
        }
    }

    //--------------------------------------------------------------------------
    // REFERENCE TYPE
    //--------------------------------------------------------------------------
    private ConstructorArg createRefArg(String className, ConstructorInfo info) {
        ConstructorArg arg = new ConstructorArg();
        arg.isPrimitive = false;
        arg.className = className;
        arg.info = info;

        if (info == null) {
            arg.type = ArgType.NULL;
            arg.info = null; // no use for info
        }
        // at this point reference may or may not be no-arg
        else {
            arg.type = ArgType.REFERENCE;
            numReferenceArgs++;
        }

        return arg;
    }

    // construct reference object
    synchronized public void addReferenceArg(String className, ConstructorInfo info) throws cMsgException {
        if (isPrimitive(className)) {
            throw new cMsgException("className must NOT correspond to primitive type");
        }

        if (className == null) {
            throw new cMsgException("className must NOT be null");
        }

        argList.add(createRefArg(className, info));
    }

    // construct reference object
    synchronized public void addReferenceArg(String className, ConstructorInfo info, int index) throws cMsgException {
        if (isPrimitive(className)) {
            throw new cMsgException("class must NOT correspond to primitive type");
        }

        if (className == null) {
            throw new cMsgException("className must NOT be null");
        }

        if (index > argList.size()) {
            argList.addLast(createRefArg(className, info));
        }
        else {
            argList.add(index, createRefArg(className, info));
        }
    }


    //--------------------------------------------------------------------------
    // DIFFICULT PART - store arg data in cmsg message
    //--------------------------------------------------------------------------
    synchronized public cMsgMessage createMessageFromArgs() {
        if (argList.size() < 1) {
            return null;
        }

        int numArgs = argList.size();

        // Clear up ambiguities in reference types that use no-arg constructors
        // (ConstructorInfo object that has no args) and those that use other
        // constructors. The confusion arises because a newly created ConstructorInfo
        // object (no args) can be passed in as a parameter and filled in later with args.
        for (ConstructorArg arg : argList) {
            if (arg.type == ArgType.REFERENCE) {
                if (arg.info.argList.size() < 1) {
                    arg.type = ArgType.REFERENCE_NOARG;
                    numReferenceArgs--;
                }
            }
        }

        cMsgMessage msg = new cMsgMessage();
        msg.setUserInt(numArgs);

        int[]    argTypes     = new int[numArgs];
        String[] classes      = new String[numArgs];
        String[] stringArgs   = new String[numPrimitiveArgs];
        cMsgMessage[] msgArgs = new cMsgMessage[numReferenceArgs];

        int cIndex = 0;
        int sIndex = 0;
        int mIndex = 0;

        for (ConstructorArg arg : argList) {
            classes[cIndex]  = arg.className;
            argTypes[cIndex] = arg.type.getValue();
            cIndex++;

            if (arg.isPrimitive) {
                if (arg.strValue != null) {
                    stringArgs[sIndex++] = arg.strValue;
                }
                else {
                    stringArgs[sIndex++] = ""+arg.charValue;
                }
            }
            else {
                if (arg.type == ArgType.REFERENCE) {
                    msgArgs[mIndex++] = arg.info.createMessageFromArgs();
                }
            }
        }

        try {
            cMsgPayloadItem item = new cMsgPayloadItem("classes", classes);
            msg.addPayloadItem(item);
            item = new cMsgPayloadItem("argTypes", argTypes);
            msg.addPayloadItem(item);
            if (numPrimitiveArgs > 0) {
                item = new cMsgPayloadItem("stringArgs", stringArgs);
                msg.addPayloadItem(item);
            }
            if (numReferenceArgs > 0) {
                item = new cMsgPayloadItem("messageArgs", msgArgs);
                msg.addPayloadItem(item);
            }
        }
        catch (cMsgException e) {/* never happen */}

        return msg;
    }

}
