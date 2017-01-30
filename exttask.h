#ifndef __EXTTASK_H
#define __EXTTASK_H

#include "server.h"

// ExternalTask
//
// Basis for all task connections external from acnetd
//
class ExternalTask : public TaskInfo {
    pid_t const pid_;
    sockaddr_in saCmd, saData;
    int contSocketErrors;
    uint32_t totalSocketErrors;
    mutable time_t lastCommandTime, lastAliveCheckTime;

    ExternalTask();

    bool checkResult(ssize_t);

 protected:

    // Client command handlers

    virtual void handleCancel(CancelCommand const *);
    virtual void handleDisconnect();
    virtual void handleDisconnectSingle();
    virtual void handleReceiveRequests();
    virtual void handleBlockRequests();
    virtual void handleRenameTask(RenameTaskCommand const *);
    virtual void handleRequestAck(RequestAckCommand const *);
    virtual void handleSendReply(SendReplyCommand const *, size_t const);
    virtual void handleIgnoreRequest(IgnoreRequestCommand const *);
    virtual void handleSendRequest(SendRequestCommand const *, size_t const);
    virtual void handleSendRequestWithTimeout(SendRequestWithTimeoutCommand const*, size_t const);
    virtual void handleSend(SendCommand const *, size_t const);
    virtual void handleTaskPid();
    virtual void handleNodeStats();
    virtual void handleKeepAlive();
    virtual void handleUnknownCommand(CommandHeader const *, size_t len);

    void commandReceived() const { lastCommandTime = now().tv_sec; }
    bool sendErrorToClient(status_t);
    bool sendAckToClient(void const*, size_t);
    bool sendMessageToClient(AcnetClientMessage*);

 public:
    ExternalTask(TaskPool&, taskhandle_t, pid_t, uint16_t, uint16_t);
    virtual ~ExternalTask() {}

    bool stillAlive(int = 0) const;
    bool isPromiscuous() const { return false; }

    pid_t pid() const { return pid_; }
    uint16_t commandPort() const { return ntohs(saCmd.sin_port); }
    uint16_t dataPort() const { return ntohs(saData.sin_port); }

    void handleClientCommand(CommandHeader const* const, size_t const);
    bool sendDataToClient(AcnetHeader const*);

    bool equals(TaskInfo const*) const;
    bool needsToBeThrottled() const { return true; }

    char const* name() const { return "ExternalTask"; }
    size_t totalProp() const;
    char const* propName(size_t) const;
    std::string propVal(size_t) const;
};

// Local Variables:
// mode:c++
// End:

#endif
