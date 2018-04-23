#include "lcltask.h"

LocalTask::LocalTask(TaskPool& taskPool, taskhandle_t handle, taskid_t id, pid_t pid,
			    uint16_t cmdPort, uint16_t dataPort) :
    ExternalTask(taskPool, handle, id, pid, cmdPort, dataPort), receiving(false)
{
}

void LocalTask::handleReceiveRequests()
{
    receiving = true;

    Ack ack;

    if (!sendAckToClient(&ack, sizeof(ack)))
	taskPool().removeTask(this);
}

void LocalTask::handleBlockRequests()
{
    receiving = false;

    while (!replies.empty())
	taskPool().rpyPool.endRpyId(*replies.begin(), ACNET_DISCONNECTED);

    Ack ack;
    if (!sendAckToClient(&ack, sizeof(ack)))
	taskPool().removeTask(this);
}
