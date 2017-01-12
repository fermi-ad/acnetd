#include "server.h"

LocalTask::LocalTask(TaskPool* taskPool, taskhandle_t handle, pid_t pid, uint16_t cmdPort,
			     uint16_t dataPort) :
    ExternalTask(taskPool, handle, pid, cmdPort, dataPort), receiving(false)
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

