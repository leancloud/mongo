// client.h

/**
*    Copyright (C) 2008 10gen Inc.
*
*    This program is free software: you can redistribute it and/or  modify
*    it under the terms of the GNU Affero General Public License, version 3,
*    as published by the Free Software Foundation.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/* Client represents a connection to the database (the server-side) and corresponds 
   to an open socket (or logical connection if pooling on sockets) from a client.

   todo: switch to asio...this will fit nicely with that.
*/

#pragma once

#include "../stdafx.h"
#include "namespace.h"
#include "lasterror.h"
#include "../util/top.h"

namespace mongo { 

    class AuthenticationInfo;
    class Database;
    class CurOp;
    class Command;
    class Client;

    extern boost::thread_specific_ptr<Client> currentClient;

    class Client : boost::noncopyable { 
    public:
        static boost::mutex clientsMutex;
        static set<Client*> clients; // always be in clientsMutex when manipulating this

        class GodScope {
            bool _prev;
        public:
            GodScope();
            ~GodScope();
        };

        /* Set database we want to use, then, restores when we finish (are out of scope)
           Note this is also helpful if an exception happens as the state if fixed up.
        */
        class Context : boost::noncopyable{
            Client * _client;
            Context * _oldContext;
            
            string _path;
            mongolock * _lock;
            bool _justCreated;

            string _ns;
            Database * _db;

            /**
             * at this point _client, _oldContext and _ns have to be set
             * _db should not have been touched
             * this will set _db and create if needed
             * will also set _client->_context to this
             */
            void _finishInit();

            
        public:
            Context(const string& ns, string path=dbpath, mongolock * lock = 0 ) 
                : _client( currentClient.get() ) , _oldContext( _client->_context ) , 
                  _path( path ) , _lock( lock ) ,
                  _ns( ns ){
                _finishInit();
            }
            
            /* this version saves the context but doesn't yet set the new one: */
            Context() 
                : _client( currentClient.get() ) , _oldContext( _client->_context ), 
                  _path( dbpath ) , _lock(0) , _justCreated(false){
                _client->_context = this;
                clear();
            }
            
            /**
             * if you are doing this after allowing a write there could be a race condition
             * if someone closes that db.  this checks that the DB is still valid
             */
            Context( string ns , Database * db );

            ~Context() {
                DEV assert( _client == currentClient.get() );
                _client->_context = _oldContext; // note: _oldContext may be null
                _client->_prevDB = _db;
            }
            
            Database* db() const {
                return _db;
            }

            const char * ns() const {
                return _ns.c_str();
            }
            
            bool justCreated() const {
                return _justCreated;
            }

            bool equals( const string& ns , const string& path=dbpath ) const {
                return _ns == ns && _path == path;
            }

            bool inDB( const string& db , const string& path=dbpath ) const {
                if ( _path != path )
                    return false;
                
                if ( db == _ns )
                    return true;

                string::size_type idx = _ns.find( db );
                if ( idx != 0 )
                    return false;
                
                return  _ns[db.size()] == '.';
            }

            void clear(){
                _ns = "";
                _db = 0;
            }

            /**
             * call before unlocking, so clear any non-thread safe state
             */
            void unlocked(){
                _db = 0;
            }

            /**
             * call after going back into the lock, will re-establish non-thread safe stuff
             */
            void relocked(){
                _finishInit();
            }
        };
        
    private:
        CurOp * const _curOp;
        Context * _context;
        bool _shutdown;
        list<string> _tempCollections;
        const char *_desc;
        bool _god;

        Database * _prevDB;
    public:
        AuthenticationInfo *ai;
        Top top;

        CurOp* curop() { return _curOp; }
        
        Context* getContext(){ return _context; }
        Database* database() {  return _context ? _context->db() : 0; }
        const char *ns() { return _context->ns(); }
        Database* prevDatabase(){ return _prevDB; }
        
        Client(const char *desc);
        ~Client();

        const char *desc() const { return _desc; }

        void addTempCollection( const string& ns ){
            _tempCollections.push_back( ns );
        }

        /* each thread which does db operations has a Client object in TLS.  
           call this when your thread starts. 
        */
        static void initThread(const char *desc);

        /* 
           this has to be called as the client goes away, but before thread termination
           @return true if anything was done
         */
        bool shutdown();

        bool isGod() const { return _god; }
    };
    
    inline Client& cc() { 
        return *currentClient.get();
    }

    /* each thread which does db operations has a Client object in TLS.  
       call this when your thread starts. 
    */
    inline void Client::initThread(const char *desc) {
        assert( currentClient.get() == 0 );
        currentClient.reset( new Client(desc) );
    }

    inline Client::GodScope::GodScope(){
        _prev = cc()._god;
        cc()._god = true;
    }

    inline Client::GodScope::~GodScope(){
        cc()._god = _prev;
    }

	/* this unlocks, does NOT upgrade. that works for our current usage */
    inline void mongolock::releaseAndWriteLock() { 
        if( !_writelock ) {

#if BOOST_VERSION >= 103500
            int s = dbMutex.getState();
            if( s != -1 ) {
                log() << "error: releaseAndWriteLock() s == " << s << endl;
                msgasserted( 12600, "releaseAndWriteLock: unlock_shared failed, probably recursive" );
            }
#endif

            _writelock = true;
            dbMutex.unlock_shared();
            dbMutex.lock();

            /* this is defensive; as we were unlocked for a moment above, 
               the Database object we reference could have been deleted:
            */
            assert( ! cc().getContext() );
            //cc().clearns();
        }
    }
    
};

