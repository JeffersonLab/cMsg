package org.jlab.coda.cMsg.remoteExec;

import org.jlab.coda.cMsg.cMsgException;
import org.jlab.coda.cMsg.cMsgMessage;
import org.jlab.coda.cMsg.cMsgPayloadItem;

import java.util.LinkedList;

/**
 * @author timmer
 * Date: Oct 28, 2010
 */
public class ConstructorInfo {

    int numPrimitiveArgs;
    int numReferenceArgs;
    LinkedList<ConstructorArg> argList = new LinkedList<ConstructorArg>();


    private class ConstructorArg {
        boolean isPrimitive;
        String className;
        ArgType type;
        String strValue;
        char charValue;
        ConstructorInfo info;
    }

    
    public ConstructorInfo() {
    }

    private boolean isPrimitive(String className) {
        if (className == null) return false;

        if (    className.equals("int")     ||
                className.equals("boolean") ||
                className.equals("char")    ||
                className.equals("byte")    ||
                className.equals("short")   ||
                className.equals("long")    ||
                className.equals("float")   ||
                className.equals("double") )  {
            return true;
        }
        return false;
    }

    //--------------------------------------------------------------------------
    // PRIMITIVES
    //--------------------------------------------------------------------------
    private ConstructorArg createPrimitiveArg(String className, String value) {
        ConstructorArg arg = new ConstructorArg();
        arg.isPrimitive = true;
        arg.className = className;
        arg.type = ArgType.PRIMITIVE;
        arg.strValue = value;
        numPrimitiveArgs++;
        return arg;
    }

    // construct primitive type object (except char) with given value
    synchronized public void addPrimitiveArg(String className, String value) throws cMsgException {
        if (!isPrimitive(className) || className.equals("char")) {
            throw new cMsgException("class must correspond to primitive type (not char)");
        }
        argList.add(createPrimitiveArg(className, value));
    }

    // construct primitive type object (except char) with given value
    synchronized public void addPrimitiveArg(String className, String value, int index) throws cMsgException {
        if (!isPrimitive(className) || className.equals("char")) {
            throw new cMsgException("class must correspond to primitive type (not char)");
        }

        // avoid throwing exception
        if (index > argList.size()) {
            argList.addLast(createPrimitiveArg(className, value));
        }
        else {
            argList.add(index, createPrimitiveArg(className, value));
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
        else if (info.argList.size() < 1) {
            arg.type = ArgType.REFERENCE_NOARG;
        }
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

        cMsgMessage msg = new cMsgMessage();
        msg.setUserInt(numArgs);

        int[]    argTypes     = new int[numArgs];
        String[] classes      = new String[numArgs];
        String[] stringArgs   = new String[numPrimitiveArgs];
        cMsgMessage[] msgArgs = new cMsgMessage[numReferenceArgs];
System.out.println("num primitives = " + numPrimitiveArgs);
System.out.println("num references = " + numReferenceArgs);

        int cIndex = 0;
        int sIndex = 0;
        int mIndex = 0;

        for (ConstructorArg arg : argList) {
            classes[cIndex]  = arg.className;
            argTypes[cIndex] = arg.type.getValue();
System.out.println("classes[" + cIndex + " = " + arg.className);
System.out.println("argType[" + cIndex + " = " + arg.type);
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
        catch (cMsgException e) {
            e.printStackTrace();
            /* never happen */}

        return msg;
    }

}
