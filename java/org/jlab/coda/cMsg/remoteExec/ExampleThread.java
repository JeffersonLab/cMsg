package org.jlab.coda.cMsg.remoteExec;

import java.awt.*;

/**
 * Class to test Commander/Executor abilities to run threads.
 *
 * @author: timmer
 * Date: Oct 22, 2010
 */
public class ExampleThread extends Thread implements IExecutorThread {
    Rectangle rec;

    public ExampleThread(Rectangle rec) {
        this.rec = rec;
    }

    public void shutItDown() {
        System.out.println("shut thread down");
        interrupt();
    }

    public void startItUp() {
        System.out.println("start thread up");
        start();
    }

    public void waitUntilDone() throws InterruptedException {
        System.out.println("wait for thread to finish");
        join();
    }

    /**
     * When run as a thread, this class must respond to interrupts
     * sent by the Commander so that it may be stopped.
     */
    public void run() {

        System.out.println("My rectangle has dimensions: width = " + rec.width +
                            ", height = " + rec.height);

        while(true) {
            System.out.println("Working ...");
            try {
                Thread.sleep(500);
            }
            catch (InterruptedException e) {
                System.out.println("Got interrupt !!!");
                break;
            }
        }

    };

}
