/*---------------------------------------------------------------------------*
*  Copyright (c) 2003        Southeastern Universities Research Association, *
*                            Thomas Jefferson National Accelerator Facility  *
*                                                                            *
*    This software was developed under a United States Government license    *
*    described in the NOTICE file included as part of this distribution.     *
*                                                                            *
*    Author:  Vardan Gyurjyan                                                *
*             gurjyan@jlab.org                  Jefferson Lab, MS-12H        *
*             Phone: (757) 269-5879             12000 Jefferson Ave.         *
*             Fax:   (757) 269-5800             Newport News, VA 23606       *
*                                                                            *
*----------------------------------------------------------------------------*/
//package org.jlab.coda.util.db;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.util.Hashtable;

/**
 * Created by IntelliJ IDEA.
 * Date: Jan 12, 2004
 * Time: 2:01:22 PM
 * To change this template use Options | File Templates.
 */
public class Jgetenv {

    private Process p;
    private BufferedReader br;
    private Hashtable m;
    private String l;
    private String re;
    private String rn;
    private String notFound = "Environmental variable is not defined";

    public Jgetenv() throws JgetenvException {
        updateInfo();
    }

    public void updateInfo() throws JgetenvException {
        try {
            p = Runtime.getRuntime().exec("printenv");
            br = new BufferedReader(new InputStreamReader(p.getInputStream()));
            m = new Hashtable();
            while ((l = br.readLine()) != null) {
                re = (l.substring(l.indexOf('=') + 1));
                rn = l.substring(0, l.indexOf('='));
                m.put(rn, re);
            }
        }
        catch (java.io.IOException e) {
            throw (new JgetenvException(e.toString()));
        }
        finally {
            try {
                br.close();
                p.destroy();
            }
            catch (java.io.IOException e) {
            }
        }
    }

    public String echo(String env) throws JgetenvException {
        if (m.containsKey(env))
            return (String) m.get(env);
        else {
            throw (new JgetenvException(notFound));
        }
    }

    public static void main(String[] args) {
        try {
            Jgetenv myJgetenv = new Jgetenv();
            System.out.println(args[0]);
            System.out.println(myJgetenv.echo(args[0]));
        }
        catch (JgetenvException a) {
            System.err.println(a);
        }
    }
}
