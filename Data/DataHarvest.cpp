/* Copyright (C) 2019 Interactive Brokers LLC. All rights reserved. This code is
 * subject to the terms and conditions of the IB API Non-Commercial License or
 * the IB API Commercial License, as applicable. */

#include "StdAfx.h"

#include "Account.h"
#include "Broker.h"
#include "ClientBrain.h"
#include "ClientData.h"
#include "HalvedPositionSMA.h"
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
constexpr unsigned SLEEP_TIME = 3;

bool inter = false;
void sigint( int sigint ) { inter = true; }

array<string, 120> StateArray = {
    // TradeManager Contribution
    "CONNECT",             // attempting a new connection
    "CONNECTSUCCESS",      // used just after a successful connection
    "CONNECTFAIL",         // a connection attempt has just failed
    "INIT",                // A new connection has just been established and the trading bot
                           // needs to be initialized
    "INITSUCCESS",         // Trading bot is fully and successfully initialized and
                           // ready to trade
    "INITFAIL",            // Something went wrong in the initialization function; leads to
                           // immediate disconnect and termination of the program
    "ACTIVETRADING",       // The trading bot is actively processing data and placing
                           // trades
    "PASSIVETRADING",      // The trading bot is actively processing data but not
                           // placing orders
    "ACCOUNTID",           // Waiting to get account ID string. Required to initialize
                           // account
    "ACCOUNTIDSUCCESS",    // Successfully received account ID string from TWS
    "ACCOUNTINIT",         // ClientAccount is being initialized by TWS
    "ACCOUNTINITSUCCESS",  // ClientAccount has just been fully and successfully
                           // initialized
    "ACCOUNTCLOSE",        // ClientAccount is closing its subscriptions
    "ACCOUNTCLOSESUCCESS", // ClientAccount has successfully closed its account
                           // subscriptions
    "ACCOUNTCLOSEFAIL",    // An error occurred when trying to close account
                           // subscriptions
    "DATAINIT", "DATAINITSUCCESS", "DATAHARVEST", "DATAHARVEST_TIMEOUT_0",
    "DATAHARVEST_TIMEOUT_1", "DATAHARVEST_TIMEOUT_2", "DATAHARVEST_DONE",
    // IB API
    "TICKDATAOPERATION", "TICKDATAOPERATION_ACK",
    "TICKOPTIONCOMPUTATIONOPERATION", "TICKOPTIONCOMPUTATIONOPERATION_ACK",
    "DELAYEDTICKDATAOPERATION", "DELAYEDTICKDATAOPERATION_ACK",
    "MARKETDEPTHOPERATION", "MARKETDEPTHOPERATION_ACK", "REALTIMEBARS",
    "REALTIMEBARS_ACK", "MARKETDATATYPE", "MARKETDATATYPE_ACK",
    "HISTORICALDATAREQUESTS", "HISTORICALDATAREQUESTS_ACK", "OPTIONSOPERATIONS",
    "OPTIONSOPERATIONS_ACK", "CONTRACTOPERATION", "CONTRACTOPERATION_ACK",
    "MARKETSCANNERS", "MARKETSCANNERS_ACK", "FUNDAMENTALS", "FUNDAMENTALS_ACK",
    "BULLETINS", "BULLETINS_ACK", "ACCOUNTOPERATIONS", "ACCOUNTOPERATIONS_ACK",
    "ORDEROPERATIONS", "ORDEROPERATIONS_ACK", "OCASAMPLES", "OCASAMPLES_ACK",
    "CONDITIONSAMPLES", "CONDITIONSAMPLES_ACK", "BRACKETSAMPLES",
    "BRACKETSAMPLES_ACK", "HEDGESAMPLES", "HEDGESAMPLES_ACK", "TESTALGOSAMPLES",
    "TESTALGOSAMPLES_ACK", "FAORDERSAMPLES", "FAORDERSAMPLES_ACK",
    "FAOPERATIONS", "FAOPERATIONS_ACK", "DISPLAYGROUPS", "DISPLAYGROUPS_ACK",
    "MISCELANEOUS", "MISCELANEOUS_ACK", "CANCELORDER", "CANCELORDER_ACK",
    "FAMILYCODES", "FAMILYCODES_ACK", "SYMBOLSAMPLES", "SYMBOLSAMPLES_ACK",
    "REQMKTDEPTHEXCHANGES", "REQMKTDEPTHEXCHANGES_ACK", "REQNEWSTICKS",
    "REQNEWSTICKS_ACK", "REQSMARTCOMPONENTS", "REQSMARTCOMPONENTS_ACK",
    "NEWSPROVIDERS", "NEWSPROVIDERS_ACK", "REQNEWSARTICLE",
    "REQNEWSARTICLE_ACK", "REQHISTORICALNEWS", "REQHISTORICALNEWS_ACK",
    "REQHEADTIMESTAMP", "REQHEADTIMESTAMP_ACK", "REQHISTOGRAMDATA",
    "REQHISTOGRAMDATA_ACK", "REROUTECFD", "REROUTECFD_ACK", "MARKETRULE",
    "MARKETRULE_ACK", "PNL", "PNL_ACK", "PNLSINGLE", "PNLSINGLE_ACK", "CONTFUT",
    "CONTFUT_ACK", "PING", "PING_ACK", "REQHISTORICALTICKS",
    "REQHISTORICALTICKS_ACK", "REQTICKBYTICKDATA", "REQTICKBYTICKDATA_ACK",
    "WHATIFSAMPLES", "WHATIFSAMPLES_ACK", "IDLE",
    // TradeManager contribution
    "INT" };

namespace ClientSpace
{
    map<int, string> StateMap;
    void             initStateMap()
    {
        int indexer = 0;
        for( auto& ind : StateArray )
        {
            StateMap[indexer] = ind;
            indexer++;
        }
    }
} // namespace ClientSpace

using namespace ClientSpace;

void ClientBrain::processMessages()
{
    if( inter )
    {
        *p_State = INT;
    }
    switch( *p_State )
    {
        case CONNECT:
            // something is wrong, should never be in this state when processing
            // messages
            spdlog::critical(
                "Client is in Connect state when processing messages. Exiting..." );
            disconnect();
            exit( CONNECT );

        case CONNECTSUCCESS:
            *p_State = INIT;
            break;

        case INIT:
            Data->init();
            *p_State = DATAHARVEST;
            break;

        case INITSUCCESS:
            *p_State = DATAHARVEST;
            break;

        case DATAHARVEST:
            Data->harvest( 0 );
            Data->harvest( 1 );
            break;

        case DATAHARVEST_TIMEOUT_0:
            if( Data->checkTimer() )
            {
                if( Data->openHistRequests.size() <= MAXIMUM_OPEN_HISTREQ )
                {
                    *p_State = DATAHARVEST;
                    Data->harvest( 2 );
                }
                else
                {
                    spdlog::info(
                        "Waiting for open requests to call back. Current size is " +
                        to_string( Data->openHistRequests.size() ) );
                }
            }
            break;

        case DATAHARVEST_TIMEOUT_1:
            if( Data->checkTimer() )
            {
                if( Data->openHistRequests.size() <= MAXIMUM_OPEN_HISTREQ )
                {
                    *p_State = DATAHARVEST;
                    Data->harvest( 3 );
                }
                else
                {
                    spdlog::info(
                        "Waiting for open requests to call back. Current size is " +
                        to_string( Data->openHistRequests.size() ) );
                }
            }
            break;

        case DATAHARVEST_TIMEOUT_2:
            if( Data->checkTimer() )
            {
                if( Data->openHistRequests.size() <= MAXIMUM_OPEN_HISTREQ )
                {
                    *p_State = DATAHARVEST;
                    Data->harvest( 4 );
                }
                else
                {
                    spdlog::info(
                        "Waiting for open requests to call back. Current size is " +
                        to_string( Data->openHistRequests.size() ) );
                }
            }
            break;

        case DATAHARVEST_DONE:
            spdlog::info( "Data harvesting has completed. Live requests will remain "
                          "active until an interrupt is received." );
            if( Data->openHistRequests.empty() )
            {
                Data->printCSVs();
                disconnect();
                exit( DATAHARVEST_DONE );
            }
            else
            {
                string openIds = "Open Requests: ";
                for( const auto& entry : Data->openHistRequests )
                {
                    openIds += to_string( entry ) + " ";
                }
                spdlog::info( openIds );
            }
            *p_State = DATAHARVEST_LIVE;
            break;

        case DATAHARVEST_LIVE:
            break;

        case ACCOUNTCLOSE:
            // Account has ended is subscriptions and is waiting for callback
            *p_State = IDLE;
            break;

        case ACCOUNTCLOSEFAIL:
            // An error occurred when trying to close account subscriptions
            spdlog::error(
                "An error occurred when trying to close account subscriptions" );
            disconnect();
            exit( ACCOUNTCLOSEFAIL );

        case IDLE:
            // do nothing
            break;

        case INT:
            spdlog::critical( "Stopping harvest and printing data to csv..." );
            Data->printCSVs();
            disconnect();
            exit( INT );
    }
    spdlog::info( "Current state is " + StateMap[*p_State] );
    std::this_thread::sleep_for( std::chrono::milliseconds( MAINLOOPDELAY ) );
    m_osSignal.waitForSignal();
    // global error status
    errno = 0;
    p_Reader->processMsgs();
}

int main( int argc, char** argv )
{
    signal( SIGINT, sigint );
    ClientSpace::initStateMap();
    unsigned    attempt = 0;
    ClientBrain client = ClientBrain();
    for( ;; )
    {
        ++attempt;
        cout << "Attempt " << attempt << " of " << MAX_ATTEMPTS << endl;
        client.connect( "", SOCKETID, CLIENTID );
        while( client.isConnected() )
        {
            client.processMessages();
        }
        if( attempt >= MAX_ATTEMPTS )
        {
            break;
        }
        if( inter )
        {
            spdlog::critical( "Process interrupted. Exiting..." );
            exit( ClientSpace::INT );
        }
        std::this_thread::sleep_for( std::chrono::seconds( SLEEP_TIME ) );
    }
    if( client.isConnected() )
    {
        client.disconnect();
    }
    return 0;
}
