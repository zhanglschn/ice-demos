// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// **********************************************************************

#include <Ice/Ice.h>

#include <PollingChat.h>
#include <ChatUtils.h>
#include <chrono>

using namespace std;
static const unsigned int maxMessageSize = 1024;

class GetUpdatesTask
{
public:

    GetUpdatesTask(const shared_ptr<PollingChat::PollingChatSessionPrx>& session) :
        _session(session)
    {
    }

    ~GetUpdatesTask()
    {
        {
            lock_guard<mutex> lock(_mutex);
            _done = true;
        }
        _cond.notify_all();

        assert(_asyncResult.valid());
        try
        {
            _asyncResult.get();
        }
        catch(const std::exception& ex)
        {
            cerr << "Update task failed with: " << ex.what() << endl;
        }
    }

    void run(int period)
    {
        assert(!_asyncResult.valid()); // run must be called just once

        _asyncResult =
            async(launch::async, [&, period]
                  {
                      unique_lock<mutex> lock(_mutex);
                      while(!_done)
                      {
                          lock.unlock();
                          try
                          {
                              auto updates = _session->getUpdates();
                              for(const auto& u : updates)
                              {
                                  auto joinedEvt = dynamic_pointer_cast<PollingChat::UserJoinedEvent>(u);
                                  if(joinedEvt)
                                  {
                                      cout << ">>>> " << joinedEvt->name << " joined." << endl;
                                  }
                                  else
                                  {
                                      auto leftEvt = dynamic_pointer_cast<PollingChat::UserLeftEvent>(u);
                                      if(leftEvt)
                                      {
                                          cout << ">>>> " << leftEvt->name << " left." << endl;
                                      }
                                      else
                                      {
                                          auto messageEvt = dynamic_pointer_cast<PollingChat::MessageEvent>(u);
                                          if(messageEvt)
                                          {
                                              cout << messageEvt->name << " > " << ChatUtils::unstripHtml(messageEvt->message) << endl;
                                          }
                                      }
                                  }
                              }
                          }
                          catch(const Ice::LocalException& ex)
                          {
                              {
                                  lock_guard<mutex> lkg(_mutex);
                                  _done = true;
                              }
                              if(!dynamic_cast<const Ice::ObjectNotExistException*>(&ex))
                              {
                                  cerr << "session lost:" << ex << endl;
                              }
                          }

                          lock.lock();
                          if(!_done)
                          {
                              _cond.wait_for(lock, chrono::seconds(period));
                          }
                      }
                  });
    }

    bool isDone() const
    {
        lock_guard<mutex> lock(const_cast<mutex&>(_mutex));
        return _done;
    }

private:

    const shared_ptr<PollingChat::PollingChatSessionPrx> _session;
    std::future<void> _asyncResult; // only used by the main thread

    bool _done = false;
    mutex _mutex;
    condition_variable _cond;
};

class ChatClient : public Ice::Application
{
public:

    ChatClient() :
        //
        // Since this is an interactive demo we don't want any signal
        // handling.
        //
        Application(Ice::SignalPolicy::NoSignalHandling)
    {
    }

    virtual int
    run(int argc, char*[]) override
    {
        if(argc > 1)
        {
            cerr << appName() << ": too many arguments" << endl;
            return EXIT_FAILURE;
        }

       auto sessionFactory =
           Ice::checkedCast<PollingChat::PollingChatSessionFactoryPrx>(
               communicator()->propertyToProxy("PollingChatSessionFactory"));

        if(!sessionFactory)
        {
            cerr << "PollingChatSessionFactory proxy is not properly configured" << endl;
            return EXIT_FAILURE;
        }

        shared_ptr<PollingChat::PollingChatSessionPrx> session;
        while(!session)
        {
            cout << "This demo accepts any user ID and password.\n";

            string id;
            cout << "user id: " << flush;
            getline(cin, id);
            id = ChatUtils::trim(id);

            string pw;
            cout << "password: " << flush;
            getline(cin, pw);
            pw = ChatUtils::trim(pw);

            try
            {
                session = sessionFactory->create(id, pw);
            }
            catch(const PollingChat::CannotCreateSessionException& ex)
            {
                cout << "Login failed:\n" << ex.reason << endl;
            }
            catch(const Ice::LocalException& ex)
            {
                cout << "Communication with the server failed:\n" << ex << endl;
            }

            if(session)
            {
                break;
            }
        }

        //
        // Override session proxy's endpoints if necessary
        //
        if(communicator()->getProperties()->getPropertyAsInt("OverrideSessionEndpoints") != 0)
        {
            session = session->ice_endpoints(sessionFactory->ice_getEndpoints());
        }

        GetUpdatesTask getUpdatesTask(session);
        getUpdatesTask.run(1);

        menu();

        auto users = session->getInitialUsers();
        cout << "Users: ";
        for(auto it = users.begin(); it != users.end();)
        {
            cout << *it;
            it++;
            if(it != users.end())
            {
                cout << ", ";
            }
        }
        cout << endl;

        try
        {
            do
            {
                string s;
                cout << "";
                getline(cin, s);
                s = ChatUtils::trim(s);
                if(!s.empty())
                {
                    if(s[0] == '/')
                    {
                        if(s == "/quit")
                        {
                            break;
                        }
                        menu();
                    }
                    else
                    {
                        if(s.size() > maxMessageSize)
                        {
                            cout << "Message length exceeded, maximum length is " << maxMessageSize << " characters.";
                        }
                        else
                        {
                            session->send(s);
                        }
                    }
                }
            }
            while(cin.good() && !getUpdatesTask.isDone());
        }
        catch(const Ice::LocalException& ex)
        {
            cerr << "Communication with the server failed:\n" << ex << endl;
            try
            {
                session->destroy();
            }
            catch(const Ice::LocalException&)
            {
            }
            return EXIT_FAILURE;
        }

        try
        {
            session->destroy();
        }
        catch(const Ice::LocalException&)
        {
        }
        return EXIT_SUCCESS;
    }

private:

    void
    menu()
    {
        cout << "enter /quit to exit." << endl;
    }
};

int
main(int argc, char* argv[])
{
#ifdef ICE_STATIC_LIBS
    Ice::registerIceSSL();
#endif
    Ice::InitializationData initData;
    initData.properties = Ice::createProperties(argc, argv);

    //
    // Set PollingChatSessionFactory if not set
    //
    if(initData.properties->getProperty("PollingChatSessionFactory").empty())
    {
        initData.properties->setProperty("Ice.Plugin.IceSSL","IceSSL:createIceSSL");
        initData.properties->setProperty("IceSSL.UsePlatformCAs", "1");
        initData.properties->setProperty("IceSSL.CheckCertName", "1");
        initData.properties->setProperty("PollingChatSessionFactory",
                                         "PollingChatSessionFactory:wss -h zeroc.com -p 443 -r /demo-proxy/chat/poll");
        initData.properties->setProperty("OverrideSessionEndpoints", "1");
    }

    ChatClient app;
    return app.main(argc, argv, initData);

}
