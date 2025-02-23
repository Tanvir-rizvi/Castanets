/*
 * Copyright 2018 Samsung Electronics Co., Ltd
 *
 * Licensed under the Flora License, Version 1.1 (the License);
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://floralicense.org/license/
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __INCLUDE_COMMON_TASK_H__
#define __INCLUDE_COMMON_TASK_H__

#ifdef WIN32

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#endif

#include "bList.h"
#include "bMessage.h"
#include "bThread.h"


typedef void (*pfCB)(int wParam, int lParam, void* pData, void* pInstance);

namespace mmBase {
class CbTask : public CbMessage, public CbThread {
 public:
  struct t_eventFromat {
    int iid;
    pfCB lpFunc;
  };

 public:
  CbTask();
  CbTask(const char* pszTaskName);
  virtual ~CbTask();

  BOOL Create(const char* pszTaskName) {
    if ((pszTaskName != NULL) && (m_bHasMsgQueue == false)) {
      if (CreateMsgQueue(pszTaskName) < 0) {
        DPRINT(COMM, DEBUG_ERROR,
               "[Warnning] Cannot Create Message Queue--Create Thread without "
               "Msg queue\n");
      } else {
        m_bHasMsgQueue = true;
      }
    }
    CbThread::SetName(pszTaskName);
    return Create();
  }
  BOOL Create();
  BOOL Destroy();

  int Send(int id,
           int wParam,
           int lParam,
           int len = 0,
           void* msgdata = NULL,
           E_MSG_TYPE type = MSG_UNICAST);
  int Send(PMSG_PACKET pPacket, E_MSG_TYPE type);
  int Send(pMsgHandle pmsgQH,
           int id,
           int wParam,
           int lParam,
           int len = 0,
           void* msgdata = NULL,
           E_MSG_TYPE type = MSG_UNICAST);
  int Send(pMsgHandle pmsgQH, PMSG_PACKET pPacket, E_MSG_TYPE type);
  int Recv(PMSG_PACKET pPacket, int i_msec);

  virtual void CheckEvent();

  virtual BOOL Subscribe(int msgid, pfCB lpFunc);
  virtual BOOL UnSubscribe(int msgid, pfCB lpFunc);
  virtual void MainLoop(void* args) {
    while (CbThread::m_bRun) {
      CheckEvent();
    }
  }

 public:
  OSAL_Mutex_Handle m_key;
  CbList<t_eventFromat> m_eventDB;

 public:
  bool m_bHasMsgQueue;
};
}
#endif

/***********************************************************************************
----------------------------------------------------------
Usage :
----------------------------------------------------------
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include "CiDataType.h"
#include "CiGlobDef.h"
#include "CiThreadBase.h"

#define SENDER_MQ_NAME	"SENDER_MQ"
#define RECVER1_MQ_NAME	"RECVER1_MQ"
#define RECVER2_MQ_NAME	"RECVER2_MQ"

class Sender : public CiThreadBase
{

public:
        Sender(){}
        Sender(const char* msgQ):CiThreadBase(msgQ){}
        virtual ~Sender(){}

        void Begin(void) {
                pReceiver1Q = GetThreadMsgInterface(RECVER1_MQ_NAME);
                pReceiver2Q = GetThreadMsgInterface(RECVER2_MQ_NAME);
                }
        void Endup(void) {
                pReceiver1Q = NULL;
                pReceiver2Q = NULL;
                }

        void MainLoop(void *args)
        {
                int msg_count=0;
                while(m_bRun)
                {
                        CMMN_PRINT(MODULE_COMMON,"Sender--Send Message\n");
                        //char *data="test message";
                        //int len=strlen(data)+1;
                        if(Send(pReceiver1Q, 0x10, msg_count++, 0x1)<0)
                                GLOB_PRINT(MODULE_GLOBAL,"Fail to Send
Message\n");
                        if(Send(pReceiver1Q, 0x11, msg_count++, 0x1)<0)
                                GLOB_PRINT(MODULE_GLOBAL,"Fail to Send
Message\n");
                        if(Send(pReceiver2Q, 0x10, msg_count++, 0x2)<0)
                                GLOB_PRINT(MODULE_GLOBAL,"Fail to Send
Message\n");


                        sleep(1);
                }
                CMMN_PRINT(MODULE_COMMON,"Sender Loop Ended\n");
        }
private:
        CiMsgBase* pReceiver1Q;
        CiMsgBase* pReceiver2Q;
};

class Receiver1 : public CiThreadBase
{
public:
        static void On_ThreadMessage1(int wParam, int lParam, void* pData, void*
pParent)
        {
                CMMN_PRINT(MODULE_COMMON,"Receive1::On_ThreadMessage1 Message
:0x10 %d %d\n",wParam,lParam);
        }
        static void On_ThreadMessage2(int wParam, int lParam, void* pData, void*
pParent)
        {
                CMMN_PRINT(MODULE_COMMON,"Receive1::On_ThreadMessage2 Message
:0x11 %d %d\n",wParam,lParam);
        }
public:
        Receiver1(){}
        Receiver1(const char* msgQ):CiThreadBase(msgQ){}
        virtual ~Receiver1(){}

        void Begin(void) {
                SubscribeMessage(0x10,(void*)this,Receiver1::On_ThreadMessage1);
                SubscribeMessage(0x11,(void*)this,Receiver1::On_ThreadMessage2);
        }
        void Endup(void) {}

        void MainLoop(void *args)
        {
                while(m_bRun)
                {
                        sleep(1);
                }
                CMMN_PRINT(MODULE_COMMON,"Receiver Loop Ended\n");
        }
private:

};

class Receiver2 : public CiThreadBase
{
public:
        static void On_ThreadMessage(int wParam, int lParam, void* pData, void*
pParent)
        {
                CMMN_PRINT(MODULE_COMMON,"Receive2::On_ThreadMessage Message
:0x10 %d %d\n",wParam,lParam);
        }
public:
        Receiver2(){}
        Receiver2(const char* msgQ):CiThreadBase(msgQ){}
        virtual ~Receiver2(){}

        void Begin(void)
{SubscribeMessage(0x10,(void*)this,Receiver2::On_ThreadMessage);}
        void Endup(void) {}

        void MainLoop(void *args)
        {
                while(m_bRun)
                {
                        sleep(1);
                }
                CMMN_PRINT(MODULE_COMMON,"Receiver Loop Ended\n");
        }
private:

};

int main(void)
{
        SetModuleDebugFlag(MODULE_ALL,true);
        StartMessageMonitor();

        Sender s_th(SENDER_MQ_NAME);
        Receiver1 r_th1(RECVER1_MQ_NAME);
        Receiver2 r_th2(RECVER2_MQ_NAME);

        s_th.Create();
        r_th1.Create();
        r_th2.Create();

        s_th.StartMainLoop(NULL);
        r_th1.StartMainLoop(NULL);
        r_th2.StartMainLoop(NULL);
        while(true)
        {

                char ch=getchar();
                if(ch=='q')
                        break;
                else if(ch=='u')
                {
                        r_th1.UnSubscribeMessage(0x10,(void*)&r_th1);
                }
                else if(ch=='v')
                {
                        r_th1.UnSubscribeMessage(0x11,(void*)&r_th1);
                }
                else if(ch=='d')
                {
                        r_th1.StopMainLoop();
                        r_th1.Destroy();
                }

        }

        return 0;
}
***********************************************************************************/
