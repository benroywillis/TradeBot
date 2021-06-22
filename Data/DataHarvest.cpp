/* Copyright (C) 2019 Interactive Brokers LLC. All rights reserved. This code is
 * subject to the terms and conditions of the IB API Non-Commercial License or
 * the IB API Commercial License, as applicable. */

#include "ClientBrain.h"
#include "twsapi/Order.h"
#include "twsapi/StdAfx.h"
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <spdlog/spdlog.h>
#include <thread>

using namespace std;

constexpr int      SOCKETID = 7497;
constexpr int      CLIENTID = 112;
constexpr unsigned MAX_ATTEMPTS = 10;
/// Program wait time between each connection attempt, in seconds
constexpr unsigned SLEEP_TIME = 3;

bool inter = false;

void sigint( int sigint )
{
    inter = true;
}

void ClientBrain::processMessages()
{
    if( inter )
    {
        *p_State = State::INT;
    }
    switch( *p_State )
    {
        case State::CONNECT:
            // something is wrong, should never be in this state when processing messages
            spdlog::critical( "Client is in CONNECT state when processing messages. Exiting..." );
            disconnect();
            exit( static_cast<int>( State::CONNECT ) );

        case State::CONNECTSUCCESS:
            *p_State = State::INIT;
            break;

        case State::INIT:
            init();
            *p_State = State::DATAHARVEST;
            break;

        case State::INITSUCCESS:
            *p_State = State::DATAHARVEST;
            break;

        case State::DATAHARVEST:
            harvest( 0 );
            harvest( 1 );
            break;

        case State::DATAHARVEST_TIMEOUT_0:
            if( checkTimer() )
            {
                if( openHistRequests.size() <= MAXIMUM_OPEN_HISTREQ )
                {
                    *p_State = State::DATAHARVEST;
                    harvest( 2 );
                }
                else
                {
                    spdlog::info( "Waiting for open requests to call back. Current size is " + to_string( openHistRequests.size() ) );
                }
            }
            break;

        case State::DATAHARVEST_TIMEOUT_1:
            if( checkTimer() )
            {
                if( openHistRequests.size() <= MAXIMUM_OPEN_HISTREQ )
                {
                    *p_State = State::DATAHARVEST;
                    harvest( 3 );
                }
                else
                {
                    spdlog::info( "Waiting for open requests to call back. Current size is " + to_string( openHistRequests.size() ) );
                }
            }
            break;

        case State::DATAHARVEST_TIMEOUT_2:
            if( checkTimer() )
            {
                if( openHistRequests.size() <= MAXIMUM_OPEN_HISTREQ )
                {
                    *p_State = State::DATAHARVEST;
                    harvest( 4 );
                }
                else
                {
                    spdlog::info( "Waiting for open requests to call back. Current size is " + to_string( openHistRequests.size() ) );
                }
            }
            break;

        case State::DATAHARVEST_DONE:
            spdlog::info( "Data harvesting has completed. Live requests will remain active until an interrupt is received." );
            if( openHistRequests.empty() )
            {
                printCSVs();
                disconnect();
                exit( static_cast<int>( State::DATAHARVEST_DONE ) );
            }
            else
            {
                string openIds = "Open Requests: ";
                for( const auto& entry : openHistRequests )
                {
                    openIds += to_string( entry ) + " ";
                }
                spdlog::info( openIds );
            }
            *p_State = State::DATAHARVEST_LIVE;
            break;

        case State::DATAHARVEST_LIVE:
            break;

        case State::ACCOUNTCLOSE:
            // Account has ended is subscriptions and is waiting for callback
            *p_State = State::IDLE;
            break;

        case State::ACCOUNTCLOSEFAIL:
            // An error occurred when trying to close account subscriptions
            spdlog::error( "An error occurred when trying to close account subscriptions" );
            disconnect();
            exit( static_cast<int>( State::ACCOUNTCLOSEFAIL ) );

        case State::IDLE:
            // do nothing
            break;

        case State::INT:
            spdlog::critical( "Stopping harvest and printing data to csv..." );
            printCSVs();
            disconnect();
            exit( static_cast<int>( State::INT ) );
    }
    spdlog::info( "Current state is " + StateMap[static_cast<int>( *p_State )] );
    std::this_thread::sleep_for( std::chrono::milliseconds( MAINLOOPDELAY ) );
    m_osSignal.waitForSignal();
    // global error status
    errno = 0;
    p_Reader->processMsgs();
}

int main( int argc, char** argv )
{
    signal( SIGINT, sigint );
    unsigned    attempt = 0;
    ClientBrain client = ClientBrain();
    while( attempt < MAX_ATTEMPTS )
    {
        ++attempt;
        if( inter )
        {
            spdlog::critical( "Process interrupted. Exiting..." );
            exit( static_cast<int>( State::INT ) );
        }
        spdlog::info( "Attempt " + to_string(attempt) + " of " + to_string(MAX_ATTEMPTS) );
        auto connection = client.connect( "", SOCKETID, CLIENTID );
        if( !connection )
        {
            spdlog::error("Connection attempt failed. Retrying...");
            continue;
        }
        std::this_thread::sleep_for( std::chrono::seconds( SLEEP_TIME ) );
        while( client.isConnected() )
        {
            client.processMessages();
        }
    }
    if( client.isConnected() )
    {
        client.disconnect();
    }
    return 0;
}
