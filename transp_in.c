#include <std.h>
#include <log.h>
#include <tsk.h>
#include <sem.h>
#include <mem.h>
#include <sio.h>
#include <sys.h>
#include <msgq.h>
#include <bcache.h>

#include "common.h"

extern LOG_Obj trace;
static MSGQ_Queue msgq_in;

void init_input_thrd() 
{
	int status;
	MSGQ_Attrs msgq_attrs;
	SEM_Handle input_sem_hdl;

	input_sem_hdl = SEM_create(0, NULL);

	if (input_sem_hdl == NULL) {
		SYS_abort("Semaphore creation failed");
	}

	msgq_attrs = MSGQ_ATTRS;
	msgq_attrs.notifyHandle = (Ptr) input_sem_hdl;
	msgq_attrs.pend = (MSGQ_Pend) SEM_pendBinary;
	msgq_attrs.post = (MSGQ_Post) SEM_postBinary;

	status = MSGQ_open("inputDSP", &msgq_in, &msgq_attrs);
	if (status != SYS_OK) {
		SYS_abort("Failed to open the inputDSP message queue");
	}
}

void input_thrd()
{
	struct cmsg *msg, *inputMsg, *outputMsg;
	int status;
	MSGQ_Queue dstMsgQueue;
	MSGQ_Queue replyMsgQueue;
	MSGQ_LocateAttrs syncLocateAttrs;
	Int msgId;

	status = MSGQ_locate("processDSP", &dstMsgQueue, NULL);
	if (status != SYS_OK) {
		SYS_abort("Failed to locate the process message queue");
	 }
	
	status = SYS_ENOTFOUND;
	while ((status == SYS_ENOTFOUND) || (status == SYS_ENODEV)) {
		syncLocateAttrs.timeout = SYS_FOREVER;
		status = MSGQ_locate("outputGPP", &replyMsgQueue, &syncLocateAttrs);
		if ((status == SYS_ENOTFOUND) || (status == SYS_ENODEV)) {
			TSK_sleep(1000);
		}
		else if (status != SYS_OK) {
			SYS_abort("Failed to locate the outputGPP message queue");
		}
	}

	status = MSGQ_alloc(0, (MSGQ_Msg *) &inputMsg, APPMSGSIZE);
	if (status != SYS_OK) {
		SYS_abort("Failed to allocate a message");
	}

	MSGQ_setMsgId((MSGQ_Msg) inputMsg, DSP_INPUTMSGID);
	status = MSGQ_put(replyMsgQueue, (MSGQ_Msg) inputMsg);
	if (status != SYS_OK) {
		SYS_abort("Failed to send a message");
	}

	inputMsg = NULL;
	outputMsg = NULL;

	for (;;) {
		status = MSGQ_get(msgq_in, (MSGQ_Msg *)&msg, SYS_FOREVER);
		if (status != SYS_OK) {
			SYS_abort("Failed to get a message from GPP");
		}
		
		msgId = MSGQ_getMsgId((MSGQ_Msg)msg);
		
		switch (msgId) {
		case GPP_OUTPUTMSGID:
			BCACHE_inv(msg->data, BUFSIZE, TRUE);
			outputMsg = msg;
			break;
		case DSP_PROCESSMSGID:
			inputMsg = msg;
			break;
		case TERMINATEMSGID:
			inputMsg = msg;
			status = MSGQ_put(replyMsgQueue, (MSGQ_Msg) inputMsg);
			if (status != SYS_OK) {
				SYS_abort("Failed to send a GPP message back");
			}
		default:
			SYS_abort("Unknown message received");
			break;
		}
		
		if ((inputMsg != NULL) && (outputMsg != NULL)) {
			status = MSGQ_put(replyMsgQueue, (MSGQ_Msg)inputMsg);
			if (status != SYS_OK) {
				SYS_abort("Failed to send a GPP message back");
			}

			MSGQ_setMsgId((MSGQ_Msg)outputMsg, DSP_INPUTMSGID);

			status = MSGQ_put(dstMsgQueue, (MSGQ_Msg)outputMsg);
			if (status != SYS_OK) {
				SYS_abort("Failed to send a message to process function");
			}
			LOG_printf(&trace, "Sending input msg to Processing TSK");

			inputMsg = NULL;
			outputMsg = NULL;
		}
	}
}
