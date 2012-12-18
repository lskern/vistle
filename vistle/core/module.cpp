#include <mpi.h>

#include <stdlib.h>
#include <stdio.h>

#include <sys/types.h>
#ifndef _WIN32
#include <unistd.h>
#else
#include<Winsock2.h>
#pragma comment(lib, "Ws2_32.lib")
#endif

#include <sstream>
#include <iostream>
#include <iomanip>

#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>

#include "object.h"
#include "message.h"
#include "messagequeue.h"
#include "parameter.h"
#include "module.h"
#include "shm.h"

using namespace boost::interprocess;

namespace vistle {

Module::Module(const std::string &n, const std::string &shmname,
      const unsigned int r,
      const unsigned int s, const int m)
   : m_name(n), m_rank(r), m_size(s), m_id(m) {

#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

   const int HOSTNAMESIZE = 64;
   char hostname[HOSTNAMESIZE];
   gethostname(hostname, HOSTNAMESIZE - 1);

   std::cerr << "  module [" << name() << "] [" << id() << "] [" << rank()
             << "/" << size() << "] started as " << hostname << ":"
#ifndef _WIN32
             << getpid() << std::endl;
#else
             << std::endl;
#endif

   try {
      Shm::attach(shmname, id(), rank(), sendMessageQueue);

      std::string smqName =
         message::MessageQueue::createName("rmq", id(), rank());
      std::string rmqName =
         message::MessageQueue::createName("smq", id(), rank());

      sendMessageQueue = message::MessageQueue::open(smqName);
      receiveMessageQueue = message::MessageQueue::open(rmqName);

   } catch (interprocess_exception &ex) {
      std::cerr << "module " << id() << " [" << rank() << "/" << size() << "] "
                << ex.what() << std::endl;
      exit(2);
   }
}

const std::string &Module::name() const {

   return m_name;
}

int Module::id() const {

   return m_id;
}

unsigned int Module::rank() const {

   return m_rank;
}

unsigned int Module::size() const {

   return m_size;
}

bool Module::createInputPort(const std::string &name) {

   std::map<std::string, ObjectList>::iterator i = inputPorts.find(name);

   if (i == inputPorts.end()) {

      inputPorts[name] = ObjectList();

      message::CreateInputPort message(id(), rank(), name);
      sendMessageQueue->getMessageQueue().send(&message, sizeof(message), 0);
      return true;
   }

   return false;
}

bool Module::createOutputPort(const std::string &name) {

   std::map<std::string, ObjectList>::iterator i = outputPorts.find(name);

   if (i == outputPorts.end()) {

      outputPorts[name] = ObjectList();

      message::CreateOutputPort message(id(), rank(), name);
      sendMessageQueue->getMessageQueue().send(&message, sizeof(message), 0);
      return true;
   }
   return false;
}

bool Module::addFileParameter(const std::string & name,
                              const std::string & value) {

   std::map<std::string, Parameter *>::iterator i =
      parameters.find(name);

   if (i == parameters.end()) {

      parameters[name] = new FileParameter(name, value);
      message::AddFileParameter message(id(), rank(), name, value);
      sendMessageQueue->getMessageQueue().send(&message, sizeof(message), 0);

      return true;
   }
   return false;
}

void Module::setFileParameter(const std::string & name,
                              const std::string & value) {

   std::map<std::string, Parameter *>::iterator i =
      parameters.find(name);

   if (i == parameters.end())
      parameters[name] = new FileParameter(name, value);
   else {
      FileParameter *param = dynamic_cast<FileParameter *>(i->second);
      if (param)
         param->setValue(value);
      else
         return;
   }

   message::SetFileParameter message(id(), rank(), id(), name, value);
   sendMessageQueue->getMessageQueue().send(&message, sizeof(message), 0);
}

std::string Module::getFileParameter(const std::string & name) const {

  std::map<std::string, Parameter *>::const_iterator i =
      parameters.find(name);

  if (i == parameters.end())
     return "";
   else {
      FileParameter *param = dynamic_cast<FileParameter *>(i->second);
      if (param)
         return param->getValue();
   }
  return "";
}

bool Module::addFloatParameter(const std::string & name,
                               const vistle::Scalar value) {

   std::map<std::string, Parameter *>::iterator i =
      parameters.find(name);

   if (i == parameters.end()) {

      parameters[name] = new FloatParameter(name, value);
      message::AddFloatParameter message(id(), rank(), name, value);
      sendMessageQueue->getMessageQueue().send(&message, sizeof(message), 0);

      return true;
   }
   return false;
}

void Module::setFloatParameter(const std::string & name,
                               const vistle::Scalar value) {

   std::map<std::string, Parameter *>::iterator i =
      parameters.find(name);

   if (i == parameters.end())
      parameters[name] = new FloatParameter(name, value);
   else {
      FloatParameter *param = dynamic_cast<FloatParameter *>(i->second);
      if (param)
         param->setValue(value);
      else
         return;
   }

   message::SetFloatParameter message(id(), rank(), id(), name, value);
   sendMessageQueue->getMessageQueue().send(&message, sizeof(message), 0);
}

vistle::Scalar Module::getFloatParameter(const std::string & name) const {

  std::map<std::string, Parameter *>::const_iterator i =
      parameters.find(name);

  if (i == parameters.end())
     return 0.0;
   else {
      FloatParameter *param = dynamic_cast<FloatParameter *>(i->second);
      if (param)
         return param->getValue();
   }
  return 0.0;
}

bool Module::addIntParameter(const std::string & name,
                             const int value) {

   std::map<std::string, Parameter *>::iterator i =
      parameters.find(name);

   if (i == parameters.end()) {

      parameters[name] = new IntParameter(name, value);
      message::AddIntParameter message(id(), rank(), name, value);
      sendMessageQueue->getMessageQueue().send(&message, sizeof(message), 0);

      return true;
   }
   return false;
}

void Module::setIntParameter(const std::string & name,
                             const int value) {

   std::map<std::string, Parameter *>::iterator i =
      parameters.find(name);

   if (i == parameters.end())
      parameters[name] = new IntParameter(name, value);
   else {
      IntParameter *param = dynamic_cast<IntParameter *>(i->second);
      if (param)
         param->setValue(value);
      else
         return;
   }

   message::SetIntParameter message(id(), rank(), id(), name, value);
   sendMessageQueue->getMessageQueue().send(&message, sizeof(message), 0);
}

int Module::getIntParameter(const std::string & name) const {

  std::map<std::string, Parameter *>::const_iterator i =
      parameters.find(name);

  if (i == parameters.end())
     return 0;
   else {
      IntParameter *param = dynamic_cast<IntParameter *>(i->second);
      if (param)
         return param->getValue();
   }
  return 0;
}

bool Module::addVectorParameter(const std::string & name,
                                const Vector & value) {

   std::map<std::string, Parameter *>::iterator i =
      parameters.find(name);

   if (i == parameters.end()) {

      parameters[name] = new VectorParameter(name, value);
      message::AddVectorParameter message(id(), rank(), name, value);
      sendMessageQueue->getMessageQueue().send(&message, sizeof(message), 0);

      return true;
   }
   return false;
}

void Module::setVectorParameter(const std::string & name,
                                const Vector & value) {

   std::map<std::string, Parameter *>::iterator i =
      parameters.find(name);

   if (i == parameters.end())
      parameters[name] = new VectorParameter(name, value);
   else {
      VectorParameter *param = dynamic_cast<VectorParameter *>(i->second);
      if (param)
         param->setValue(value);
      else
         return;
   }

   message::SetVectorParameter message(id(), rank(), id(), name, value);
   sendMessageQueue->getMessageQueue().send(&message, sizeof(message), 0);
}

Vector Module::getVectorParameter(const std::string & name) const {

  std::map<std::string, Parameter *>::const_iterator i =
      parameters.find(name);

  if (i == parameters.end())
     return Vector(0.0, 0.0, 0.0);
   else {
      VectorParameter *param = dynamic_cast<VectorParameter *>(i->second);
      if (param)
         return param->getValue();
   }
  return Vector(0.0, 0.0, 0.0);
}

bool Module::addObject(const std::string & portName, vistle::Object::const_ptr object) {

   if (!object)
      return false;

   std::map<std::string, ObjectList>::iterator i = outputPorts.find(portName);
   if (i != outputPorts.end()) {
      // XXX: this was the culprit keeping the final object reference around
      //i->second.push_back(object);
      message::AddObject message(id(), rank(), portName, object);
      sendMessageQueue->getMessageQueue().send(&message, sizeof(message), 0);
      return true;
   }
   return false;
}

Module::ObjectList Module::getObjects(const std::string &portName) {

   ObjectList objects;
   std::map<std::string, ObjectList>::iterator i = inputPorts.find(portName);

   if (i != inputPorts.end()) {

      std::list<shm_handle_t>::iterator shmit;
      for (ObjectList::const_iterator it = i->second.begin(); it != i->second.end(); it++) {
         Object::const_ptr object = *it;
         if (object.get())
            objects.push_back(object);
      }
   }
   return objects;
}

void Module::removeObject(const std::string &portName, vistle::Object::const_ptr object) {

   bool erased = false;
   shm_handle_t handle = object->getHandle();
   std::map<std::string, ObjectList>::iterator i = inputPorts.find(portName);

   if (i != inputPorts.end()) {
      ObjectList &olist = i->second;

      for (ObjectList::iterator it = olist.begin(); it != olist.end(); ) {
         if (handle == (*it)->getHandle()) {
            erased = true;
            //object->unref(); // XXX: doesn't erasing the it handle that already?
            it = olist.erase(it);
         } else
            ++it;
      }
      if (!erased)
         std::cerr << "Module " << id() << " removeObject didn't find"
            " object [" << object->getName() << "]" << std::endl;
   } else
      std::cerr << "Module " << id() << " removeObject didn't find port ["
                << portName << "]" << std::endl;
}

bool Module::hasObject(const std::string &portName) const {

   std::map<std::string, ObjectList>::const_iterator i = inputPorts.find(portName);

   if (i != inputPorts.end()) {

      return !i->second.empty();
   }

   return false;
}

vistle::Object::const_ptr Module::takeFirstObject(const std::string &portName) {

   std::map<std::string, ObjectList>::iterator i = inputPorts.find(portName);

   if (i != inputPorts.end() && !i->second.empty()) {

      Object::const_ptr obj = i->second.front();
      i->second.pop_front();
      return obj;
   }

   return vistle::Object::ptr();
}

bool Module::addInputObject(const std::string & portName,
                            Object::const_ptr object) {

   std::map<std::string, ObjectList>::iterator i = inputPorts.find(portName);

   if (i != inputPorts.end()) {
      i->second.push_back(object);
      return true;
   }
   return false;
}


bool Module::dispatch() {

   size_t msgSize;
   unsigned int priority;
   char msgRecvBuf[message::Message::MESSAGE_SIZE];

   receiveMessageQueue->getMessageQueue().receive(
                                               (void *) msgRecvBuf,
                                               message::Message::MESSAGE_SIZE,
                                               msgSize, priority);

   vistle::message::Message *message = (vistle::message::Message *) msgRecvBuf;

   bool again = handleMessage(message);
   if (!again) {
      vistle::message::ModuleExit m(id(), rank());
      sendMessage(&m);
   }

   //sleep(1);

   return again;
}


void Module::sendMessage(const message::Message *message) {

   sendMessageQueue->getMessageQueue().send(message, message->getSize(), 0);
}


bool Module::handleMessage(const vistle::message::Message *message) {

   switch (message->getType()) {

      case vistle::message::Message::PING: {

         const vistle::message::Ping *ping =
            static_cast<const vistle::message::Ping *>(message);

         std::cerr << "    module [" << name() << "] [" << id() << "] ["
                   << rank() << "/" << size() << "] ping ["
                   << ping->getCharacter() << "]" << std::endl;
         vistle::message::Pong m(id(), rank(), ping->getCharacter(), ping->getModuleID());
         sendMessage(&m);
         break;
      }

      case vistle::message::Message::PONG: {

         const vistle::message::Pong *pong =
            static_cast<const vistle::message::Pong *>(message);

         std::cerr << "    module [" << name() << "] [" << id() << "] ["
                   << rank() << "/" << size() << "] pong ["
                   << pong->getCharacter() << "]" << std::endl;
         break;
      }

      case message::Message::QUIT: {

         const message::Quit *quit =
            static_cast<const message::Quit *>(message);
         (void) quit;
         return false;
         break;
      }

      case message::Message::KILL: {

         const message::Kill *kill =
            static_cast<const message::Kill *>(message);
         if (kill->getModule() == id()) {
            return false;
         } else {
            std::cerr << "module [" << name() << "] [" << id() << "] ["
               << rank() << "/" << size() << "]" << ": received invalid Kill message" << std::endl;
         }
         break;
      }

      case message::Message::COMPUTE: {

         const message::Compute *comp =
            static_cast<const message::Compute *>(message);
         (void) comp;
         /*
         std::cerr << "    module [" << name() << "] [" << id() << "] ["
                   << rank() << "/" << size << "] compute" << std::endl;
         */
         message::Busy busy(id(), rank());
         sendMessage(&busy);
         bool ret = compute();
         message::Idle idle(id(), rank());
         sendMessage(&idle);
         return ret;
         break;
      }

      case message::Message::ADDOBJECT: {

         const message::AddObject *add =
            static_cast<const message::AddObject *>(message);
         addInputObject(add->getPortName(), add->takeObject());
         break;
      }

      case message::Message::SETFILEPARAMETER: {

         const message::SetFileParameter *param =
            static_cast<const message::SetFileParameter *>(message);

         setFileParameter(param->getName(), param->getValue());
         break;
      }

      case message::Message::SETFLOATPARAMETER: {

         const message::SetFloatParameter *param =
            static_cast<const message::SetFloatParameter *>(message);

         setFloatParameter(param->getName(), param->getValue());
         break;
      }

      case message::Message::SETINTPARAMETER: {

         const message::SetIntParameter *param =
            static_cast<const message::SetIntParameter *>(message);

         setIntParameter(param->getName(), param->getValue());
         break;
      }

      case message::Message::SETVECTORPARAMETER: {

         const message::SetVectorParameter *param =
            static_cast<const message::SetVectorParameter *>(message);

         setVectorParameter(param->getName(), param->getValue());
         break;
      }

      default:
         std::cerr << "    module [" << name() << "] [" << id() << "] ["
                   << rank() << "/" << size() << "] unknown message type ["
                   << message->getType() << "]" << std::endl;

         break;
   }

   return true;
}

Module::~Module() {

   std::cerr << "  module [" << name() << "] [" << id() << "] [" << rank()
             << "/" << size() << "] quit" << std::endl;

   MPI_Barrier(MPI_COMM_WORLD);
   MPI_Finalize();
}

} // namespace vistle
