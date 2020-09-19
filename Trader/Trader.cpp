/* Copyright (C) 2019 Interactive Brokers LLC. All rights reserved. This code is
 * subject to the terms and conditions of the IB API Non-Commercial License or
 * the IB API Commercial License, as applicable. */

#include "StdAfx.h"

#include "Account.h"
#include "Broker.h"
#include "ClientAccount.h"
#include "ClientBrain.h"
#include "ClientBroker.h"
#include "ClientData.h"
#include "Data.h"
#include "DataStruct.h"
#include "DataTypes.h"
#include "Execution.h"
#include "HalvedPositionSMA.h"
#include "Order.h"
#include "SMA.h"
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
    "DISCONNECTED",        // disconnected after initialization
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
    "DATAHARVEST_TIMEOUT_1", "DATAHARVEST_TIMEOUT_2", "DATAHARVEST_LIVE",
    "DATAHARVEST_DONE", "DATA_NEXT", "TRADING", "ORDERING",
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
vector<pair<Contract, Order>> trades;

void ClientBrain::processMessages()
{
    if( inter )
    {
        *p_State = INT;
    }
    switch( *p_State )
    {
        case DISCONNECTED:
            // run connection protocol and try again
            return;

        case CONNECTSUCCESS:
            *p_State = INIT;
            init();
            break;

        case INIT:
            // waiting on callbacks from Account, Data class
            if( Account->valid && Data->valid )
            {
                cout << "Bot successfully initialized with account info: " << endl;
                for( auto* const pos : Account->positions )
                {
                    cout << pos->getContract()->symbol << "," << pos->getContract()->secType
                         << "," << pos->getAvgPrice() << "," << pos->getPositionSize()
                         << endl;
                }
                *p_State = INITSUCCESS;
            }
            break;

        case INITSUCCESS:
            *p_State = DATA_NEXT;
            break;

        case ACCOUNTINITSUCCESS:
            *p_State = DATA_NEXT;
            break;

        case TRADING:
            Strategy->ProcessNextTick( static_pointer_cast<BTAccount>( Account ),
                                       static_pointer_cast<BTBroker>( Broker ), Data,
                                       trades );
            for( const auto& trade : trades )
            {
                spdlog::info( "Setting up a trade for symbol " + trade.first.symbol +
                              ", sectype " + trade.first.secType );
                *p_State = ORDERING;
            }
            break;

        case ORDERING:
            for( auto& trade : trades )
            {
                spdlog::info( "Requesting trade for symbol " + trades[0].first.symbol +
                              ", sectype " + trades[0].first.secType );
                Broker->placeOrder( trade );
            }
            *p_State = DATA_NEXT;
            trades.clear();
            break;

        case DATA_NEXT:
            if( Data->updated() )
            {
                *p_State = TRADING;
            }
            break;

        case IDLE:
            // do nothing
            break;

        case INT:
            spdlog::critical( "Process interrupted. Exiting..." );
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

/// SMA indicator lengths
constexpr int fast = 50;
constexpr int slow = 250;

int main( int argc, char** argv )
{
    signal( SIGINT, sigint );
    ClientSpace::initStateMap();
    unsigned attempt = 0;
    trades = vector<pair<Contract, Order>>();
    auto indicators = vector<BTIndicator*>();
    auto SMAF = make_unique<SMA>( fast );
    auto SMAS = make_unique<SMA>( slow );
    indicators.push_back( SMAF.get() );
    indicators.push_back( SMAS.get() );
    auto Strategy = make_shared<HPSMA>();
    auto Data = make_shared<ClientData>( indicators );
    auto client = ClientBrain( Data, Strategy );
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
