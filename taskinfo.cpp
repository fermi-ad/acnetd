#include <algorithm>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#ifndef NO_REPORT
#include <iomanip>
#endif
#include "server.h"

// Creates the object by initializing some default values. Since the default values reflect an unused object, it adds itself
// to the free list.

TaskInfo::TaskInfo(TaskPool* taskPool, taskhandle_t h) :
    boot(now().tv_sec), taskPool_(*taskPool), handle_(h),
    pendingRequests(0), maxPendingRequests(0)
{
}

time_t TaskInfo::connectedTime() const
{
    return now().tv_sec - boot;
}

#ifndef DEBUG
#define SZ_ATTR	__attribute__((unused))
#else
#define SZ_ATTR
#endif

bool TaskInfo::decrementPendingRequests()
{
    if (pendingRequests > 0) {
	--pendingRequests;
	return true;
    } else
	return false;
}

bool TaskInfo::testPendingRequestsAndIncrement()
{
    if (pendingRequests < maxPendingRequestsAccepted) {
	if (needsToBeThrottled() && ++pendingRequests > maxPendingRequests)
	    maxPendingRequests = pendingRequests;
	return true;
    } else
	return false;
}

#ifndef NO_REPORT
void TaskInfo::report(std::ostream& os) const
{
    os <<
	"\t\t<table class=\"dump\">\n"
	"\t\t\t<colgroup>\n"
	"\t\t\t\t<col class=\"label\"/>\n"
	"\t\t\t\t<col/>\n"
	"\t\t\t</colgroup>\n"
	"\t\t\t<thead>\n";

    os <<
	"\t\t\t<tr><td colspan=\"2\">Task " << rtoa_strip(handle().raw());

    os <<
	' ' << ((acceptsUsm() || acceptsRequests()) ? "*RUM* " : "") << "(0x" <<
	std::setw(8) << std::setfill('0') << std::hex << handle().raw() << ") " << name() << "</td></tr>\n"
	"\t\t\t</thead>\n";

    os <<
	"\t\t\t<tbody>\n"
	"\t\t\t<tr class=\"even\"><td class=\"label\">ID</td><td>" << std::dec << (uint32_t) id() << "</td></tr>\n"
	"\t\t\t<tr><td class=\"label\">PID</td><td>" << pid() << "</td></tr>\n" <<
	"\t\t\t<tr class=\"even\"><td class=\"label\">Pending Request</td><td>" << pendingRequests << "</td></tr>\n" <<
	"\t\t\t<tr><td class=\"label\">Max Pending Requests</td><td>" << maxPendingRequests << "</td></tr>\n" <<
	"\t\t\t<tr class=\"even\"><td class=\"label\">USMs Transmitted</td><td>" << (uint32_t) statUsmXmt << "</td></tr>\n" <<
	"\t\t\t<tr><td class=\"label\">Requests Transmitted</td><td>" << (uint32_t) statReqXmt << "</td></tr>\n" <<
	"\t\t\t<tr class=\"even\"><td class=\"label\">Replies Transmitted</td><td>" << (uint32_t) statRpyXmt << "</td></tr>\n" <<
	"\t\t\t<tr><td class=\"label\">USMs Received</td><td>" << (uint32_t) statUsmRcv << "</td></tr>\n" <<
	"\t\t\t<tr class=\"even\"><td class=\"label\">Requests Received</td><td>" << (uint32_t) statReqRcv << "</td></tr>\n" <<
	"\t\t\t<tr><td class=\"label\">Replies Received</td><td>" << (uint32_t) statRpyRcv << "</td></tr>\n" <<
	"\t\t\t<tr class=\"even\"><td class=\"label\">Dropped Packets</td><td>" << (uint32_t) statLostPkt << "</td></tr>\n" <<
	"\t\t\t<tr><td class=\"label\">Connected</td><td>";

    printElapsedTime(os, now().tv_sec - boot);

    os << "</td></tr>\n";

    for (size_t ii = 0; ii < totalProp(); ++ii)
	os << "\t\t\t<tr" << (ii % 2 ? "" : " class=\"even\"") << "><td class=\"label\">" << propName(ii) <<
	    "</td><td>" << propVal(ii) << "</td></tr>\n";

    os << "\t\t\t</tbody>\n" << std::hex;

    if (!requests.empty()) {
	os << "\t\t\t<thead>\n"
	    "\t\t\t<tr><td colspan=\"2\">Requests</td></tr>\n"
	    "\t\t\t</thead>\n"
	    "\t\t\t<tbody>\n"
	    "\t\t\t<tr><td colspan=\"2\"><tt>";

	for (auto ii = requests.begin(); ii != requests.end(); ++ii) {
	    if (ii != requests.begin())
		os << ", ";
	    os << "0x" << std::setw(4) << std::setfill('0') << *ii;
	}

	os << "</tt></td></tr>\n"
	    "\t\t\t</tbody>\n";
    }

    if (!replies.empty()) {
	os << "\t\t\t<thead>\n"
	    "\t\t\t<tr><td colspan=\"2\">Replies</td></tr>\n"
	    "\t\t\t</thead>\n"
	    "\t\t\t<tbody>\n"
	    "\t\t\t<tr><td colspan=\"2\"><tt>";

	for (auto ii = replies.begin(); ii != replies.end(); ++ii) {
	    if (ii != replies.begin())
		os << ", ";
	    os << "0x" << std::setw(4) << std::setfill('0') << *ii;
	}

	os << "</tt></td></tr>\n"
	    "\t\t\t</tbody>\n";
    }
    os << std::setw(0) << std::setfill(' ') << std::dec << "\t\t</table>\n"; // \t\t</div>";
}
#endif

// Local Variables:
// mode:c++
// fill-column:125
// End:
