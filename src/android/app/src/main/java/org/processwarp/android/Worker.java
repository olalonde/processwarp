package org.processwarp.android;

import android.os.Handler;

import junit.framework.Assert;

import java.util.concurrent.locks.ReentrantLock;

public class Worker implements Runnable {
    /**
     * Delegate for implement by service.
     */
    public interface Delegate {
        void workerRelayCommand(Worker caller, CommandPacket packet);
    }

    /** Delegate instance for pass packet. */
    private Delegate delegate;
    /** Handler for run loop. */
    private Handler handler;
    /** Process-id bundled for process. */
    private String myPid;
    /** Node-id of this node. */
    private String myNid;
    /** Mutex for execute native methods serial. */
    private ReentrantLock lock = new ReentrantLock();
    /** Stop flag. True if worker has stop explicit. */
    private boolean isStoped = false;

    /**
     * Initialize worker and create VM by native method.
     * @param delegate Delegate for implement by service.
     * @param myNid Node-id of this node.
     * @param myPid Process-id for new vm.
     * @param rootTid Root thread-id for new vm.
     * @param procAddr Address of process information for new vm.
     * @param masterNid Node-id of master node for new vm.
     * @param name Process name for new vm.
     */
    public void initialize(Delegate delegate, String myNid, String myPid,
                           long rootTid, long procAddr, String masterNid, String name) {
        this.delegate = delegate;
        this.myNid = myNid;
        this.myPid = myPid;

        lock.lock();
        try {
            Assert.assertNotNull(myNid);
            Assert.assertNotNull(myPid);
            Assert.assertNotNull(masterNid);
            vmInitialize(this, myNid, myPid, rootTid, procAddr, masterNid, name);
        } finally {
            lock.unlock();
        }
        this.handler = new Handler();
    }

    /**
     * Get Process-id bundled for process.
     * @return Process-id bundled for process.
     */
    public String getPid() {
        return myPid;
    }

    /**
     * Execute VM through JNI if not stop.
     */
    @Override
    public void run() {
        // Check and change stop flag to don't continue to stop.
        if (isStoped) {
            isStoped = false;
            return;
        }

        lock.lock();
        try {
            Assert.assertNotNull(myPid);
            vmExecute(myPid);
        } finally {
            lock.unlock();
        }

        // handler.postDelayed(this, 1);
        handler.post(this);
    }

    /**
     * Stop vm.
     */
    public void stop() {
        isStoped = true;
    }

    /**
     * When receive packet from another node or another module to worker, relay it by NDK.
     * @param pid Process-id bundled to packet.
     * @param dstNid Destination node-id.
     * @param srcNid Source node-id, should be my node-id, THIS, or NONE.
     * @param module Target module.
     * @param content Packet content string of JSON.
     */
    public void relayWorkerCommand(String pid, String dstNid, String srcNid,
                                   int module, String content) {

        handler.post(new Runnable() {
            private String pid;
            private String dstNid;
            private String srcNid;
            private int module;
            private String content;

            public Runnable initialize(String pid, String dstNid, String srcNid, int module, String content) {
                this.pid = pid;
                this.dstNid = dstNid;
                this.srcNid = srcNid;
                this.module = module;
                this.content = content;

                return this;
            }

            @Override
            public void run() {
                lock.lock();
                try {
                    vmRelayCommand(pid, dstNid, srcNid, module, content);
                } finally {
                    lock.unlock();
                }
            }
        }.initialize(pid, dstNid, srcNid, module, content));
    }

    /**
     * When worker require send a packet, relay command by command type.
     * @param pid Process-id bundled to packet.
     * @param dstNid Destination node-id.
     * @param srcNid Source node-id, should be my node-id, THIS, or NONE.
     * @param module Target module.
     * @param content Packet content string of JSON.
     */
    private void workerSendCommand(String pid, String dstNid, String srcNid,
                                   int module, String content) {
        CommandPacket packet = new CommandPacket();
        packet.pid = pid;
        packet.dstNid = dstNid;
        packet.srcNid = srcNid;
        packet.module = module;
        packet.content = content;
        delegate.workerRelayCommand(this, packet);
    }

    private native void vmInitialize(Worker worker, String myNid, String myPid, long rootTid,
                                         long procAddr, String masterNid, String name);
    private native void vmRelayCommand(String pid, String dstNid, String srcNid,
                                           int module, String content);
    private native void vmExecute(String pid);
    private native void vmQuit(String pid);
}
