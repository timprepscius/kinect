//
//  Server.cpp
//  KinectServer1
//
//  Created by Timothy Prepscius on 3/19/13.
//  Copyright (c) 2013 Timothy Prepscius. All rights reserved.
//

#include "websocketpp/websocketpp.hpp"

#include <cstring>
#include <set>
#include "UserTracker.h"
#include <json/writer.h>

#include <boost/functional.hpp>
#include <boost/bind.hpp>

using websocketpp::server;


void generate_telemetry(boost::function<bool(const std::string&)> callback)
{
	UserTracker userTracker;
    userTracker.initialize();
    
    for (;;)
	{
        // do some work
        userTracker.tick();
        
        // broadcast state
        std::stringstream m;
        json::Writer::Write(userTracker.getUsers(), m);
        
        if (callback(m.str())) {
            break;
        }
        
        // wait
        boost::this_thread::sleep(boost::posix_time::milliseconds(50));
    }
    
    userTracker.shutdown();
}

class telemetry_server_handler : public server::handler {
public:
    typedef telemetry_server_handler type;
    typedef boost::shared_ptr<type> ptr;
    
    telemetry_server_handler() : m_done(false),m_value(0) {
        
        boost::function<bool(const std::string&)> callback = boost::bind(&type::on_tick,this,_1);
        
        // start a thread that will generate telemetry independently and call
        // this handler back when it has new data to send.
        m_telemetry_thread.reset(new boost::thread(
                                                   boost::bind(
                                                               &generate_telemetry,
                                                               callback
                                                               )
                                                   ));
    }
    
    // If the handler is going away set done to true and wait for the thread
    // to exit.
    ~telemetry_server_handler() {
        {
            boost::lock_guard<boost::mutex> guard(m_mutex);
            m_done = true;
        }
        m_telemetry_thread->join();
    }
    
    /// Function that we pass to the telemetry thread to broadcast the new
    /// state. It returns the global "are we done" value so we can control when
    /// the thread stops running.
    bool on_tick(const std::string& msg) {
        boost::lock_guard<boost::mutex> guard(m_mutex);
        
        std::set<connection_ptr>::iterator it;
        
        for (it = m_connections.begin(); it != m_connections.end(); it++) {
            (*it)->send(msg);
        }
        
        return m_done;
    }
    
    // register a new client
    void on_open(connection_ptr con) {
        boost::lock_guard<boost::mutex> guard(m_mutex);
        m_connections.insert(con);
    }
    
    // remove an exiting client
    void on_close(connection_ptr con) {
        boost::lock_guard<boost::mutex> guard(m_mutex);
        m_connections.erase(con);
    }
private:
    bool                                m_done;
    size_t                              m_value;
    std::set<connection_ptr>            m_connections;
    
    boost::mutex                        m_mutex;    // guards m_connections
    boost::shared_ptr<boost::thread>    m_telemetry_thread;
};

int main(int argc, char* argv[]) {
    
    unsigned short port = 9007;
    
    if (argc == 2) {
        port = atoi(argv[1]);
        
        if (port == 0) {
            std::cout << "Unable to parse port input " << argv[1] << std::endl;
            return 1;
        }
    }
    
    try {
        server::handler::ptr handler(new telemetry_server_handler());
        server endpoint(handler);
        
        endpoint.alog().unset_level(websocketpp::log::alevel::ALL);
        endpoint.elog().unset_level(websocketpp::log::elevel::ALL);
        
        endpoint.alog().set_level(websocketpp::log::alevel::CONNECT);
        endpoint.alog().set_level(websocketpp::log::alevel::DISCONNECT);
        
        endpoint.elog().set_level(websocketpp::log::elevel::RERROR);
        endpoint.elog().set_level(websocketpp::log::elevel::FATAL);
        
        std::cout << "Starting WebSocket telemetry server on port " << port << std::endl;
        endpoint.listen(port);
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
    
    return 0;
}
