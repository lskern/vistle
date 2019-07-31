#ifndef USERINTERFACE_H
#define USERINTERFACE_H

#include <iostream>
#include <list>
#include <map>
#include <exception>

#include "export.h"
#include <core/parameter.h>
#include <core/port.h>
#include <core/porttracker.h>
#include <core/statetracker.h>
#include <core/message.h>

#include <boost/asio.hpp>
#include <mutex>
#include <condition_variable>

namespace vistle {

class UserInterface;

class V_UIEXPORT FileBrowser {
    friend class UserInterface;
public:
    virtual ~FileBrowser();
    int id() const;

    bool sendMessage(const message::Message &message, const std::vector<char> *payload=nullptr);
    virtual bool handleMessage(const message::Message &message, const std::vector<char> &payload) = 0;

private:
    UserInterface *m_ui = nullptr;
    int m_id = -1;
};

class V_UIEXPORT UserInterface {

 public:
   UserInterface(const std::string &host, const unsigned short port, StateObserver *observer=nullptr);
   virtual ~UserInterface();

   bool isInitialized() const;

   void stop();
   void cancel();

   virtual bool dispatch();
   bool sendMessage(const message::Message &message, const std::vector<char> *payload=nullptr);

   int id() const;
   const std::string &host() const;

   const std::string &remoteHost() const;
   unsigned short remotePort() const;

   boost::asio::ip::tcp::socket &socket();
   const boost::asio::ip::tcp::socket &socket() const;

   bool tryConnect();
   bool isConnected() const;

   StateTracker &state();

   bool getLockForMessage(const message::uuid_t &uuid);
   bool getMessage(const message::uuid_t &uuid, message::Message &msg);

   void registerObserver(StateObserver *observer);
   void registerFileBrowser(FileBrowser *browser);
   void removeFileBrowser(FileBrowser *browser);

 protected:

   int m_id;
   std::string m_hostname;
   std::string m_remoteHost;
   unsigned short m_remotePort;
   bool m_isConnected;

   StateTracker m_stateTracker;

   bool handleMessage(const message::Message *message, const std::vector<char> &payload);

   boost::asio::io_service m_ioService;
   boost::asio::ip::tcp::socket m_socket;

   struct RequestedMessage {
      std::mutex mutex;
      std::condition_variable cond;
      bool received;
      std::vector<char> buf;

      RequestedMessage(): received(false) {}
   };

   typedef std::map<message::uuid_t, std::shared_ptr<RequestedMessage>> MessageMap;
   MessageMap m_messageMap;
   std::mutex m_messageMutex; //< protect access to m_messageMap
   bool m_locked = false;
   std::vector<message::Buffer> m_sendQueue;
   mutable std::mutex m_mutex;
   bool m_initialized = false;

   int m_fileBrowserCount = 0;
   std::vector<FileBrowser *> m_fileBrowser;
};

} // namespace vistle

#endif
