#include "ClientBrain.h"
#include "TradeBase/Data.h"
#include "TradeBase/Execution.h"
#include "TradeBase/Strategy.h"
#include "twsapi/Contract.h"
#include "twsapi/EClientSocket.h"
#include "twsapi/Execution.h"
#include "twsapi/Order.h"
#include "twsapi/OrderState.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <spdlog/spdlog.h>
#include <thread>

using namespace std;

// General wait time between operations, in milliseconds
constexpr unsigned int WAIT_TIME = 100;
// Wait time between data feed request operations, in milliseconds
constexpr unsigned int INIT_WAIT_TIME = 1000;
// Wait time between historical data requests, in milliseconds
constexpr unsigned int REQ_WAIT_TIME = 200;

ClientBrain::ClientBrain() : m_osSignal( 2000 ), p_Client( make_shared<EClientSocket>( this, &m_osSignal ) ), p_State( make_shared<State>( State::CONNECT ) ), m_sleepDeadline( 0 ), p_OrderId( make_shared<OrderId>( 0 ) ), p_Reader( nullptr ), p_ExtraAuth( make_shared<bool>( false ) )
{
    Strategy = make_shared<TradeBase::TBStrategy>();
    reqId = 10000;
    openHistRequests = set<long>();
    openDataLines = set<long>();
    updatedLines = set<long>();
    TimeLine = TradeBase::TimeMap();
}

ClientBrain::ClientBrain( shared_ptr<TradeBase::TBStrategy> newStrategy ) : m_osSignal( 2000 ), p_Client( make_shared<EClientSocket>( this, &m_osSignal ) ), p_State( make_shared<State>( State::CONNECT ) ), m_sleepDeadline( 0 ), p_OrderId( make_shared<OrderId>( 0 ) ), p_Reader( nullptr ), p_ExtraAuth( make_shared<bool>( false ) )
{
    Strategy = move( newStrategy );
    reqId = 10000;
    openHistRequests = set<long>();
    openDataLines = set<long>();
    updatedLines = set<long>();
    TimeLine = TradeBase::TimeMap();
}

long ClientBrain::getNextConId()
{
    return conId++;
}

long ClientBrain::getNextOrderId()
{
    return orderId++;
}

long ClientBrain::getNextVectorId()
{
    return vectorId++;
}

long ClientBrain::getNextReqId()
{
    return reqId++;
}

void ClientBrain::setConnectOptions( const std::string& connectOptions )
{
    p_Client->setConnectOptions( connectOptions );
}

void ClientBrain::init()
{
    accountInit();
    dataInit();
}

bool ClientBrain::connect( const char* host, int port, int clientId )
{
    clientID = clientId;
    *p_State = State::CONNECT;
    string hostName = !( ( host != nullptr ) && ( *host ) != 0 ) ? "127.0.0.1" : host;
    spdlog::info( "Connecting to " + hostName + ": " + to_string( port ) + " clientID: " + to_string( clientId ) );
    bool bRes = p_Client->eConnect( host, port, clientId, *p_ExtraAuth );
    if( bRes )
    {
        spdlog::info( "Connected to " + p_Client->host() + ": " + to_string( p_Client->port() ) + " clientID: " + to_string( clientId ) );
        p_Reader = make_shared<EReader>( p_Client.get(), &m_osSignal );
        p_Reader->start();
        *p_State = State::CONNECTSUCCESS;
    }
    else
    {
        *p_State = State::CONNECTFAIL;
        spdlog::error( "Cannot connect to " + p_Client->host() + ": " + to_string( p_Client->port() ) + " clientID: " + to_string( clientId ) );
    }
    // if this isn't here the client will immediately disconnect
    this_thread::sleep_for( chrono::milliseconds( WAIT_TIME ) );
    return bRes;
}

void ClientBrain::disconnect() const
{
    p_Client->eDisconnect();
    *p_State = State::DISCONNECTED;
    spdlog::info( "Disconnected" );
}

bool ClientBrain::isConnected() const
{
    return p_Client->isConnected();
}

void ClientBrain::connectionClosed()
{
    spdlog::info( "Connection Closed" );
    *p_State = State::DISCONNECTED;
}

void ClientBrain::connectAck()
{
    if( !*p_ExtraAuth && p_Client->asyncEConnect() )
    {
        p_Client->startApi();
    }
}

void ClientBrain::reqHeadTimestamp()
{
    p_Client->reqHeadTimestamp( 14001, Contract(), "MIDPOINT", 1, 1 );
    std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
    p_Client->cancelHeadTimestamp( 14001 );
}

/// Callback to reqHeadTimestamp
void ClientBrain::headTimestamp( int reqId, const std::string& headTimestamp )
{
    spdlog::info( "Head time stamp. ReqId: " + to_string( reqId ) + " - Head time stamp: " + headTimestamp );
}

void ClientBrain::reqCurrentTime()
{
    // set ping deadline to "now + n seconds"
    m_sleepDeadline = time( nullptr ) + PING_DEADLINE;
    p_Client->reqCurrentTime();
}

/// Callback to reqCurrentTime()
void ClientBrain::currentTime( long time )
{
    auto       t = (time_t)time;
    struct tm* timeinfo = localtime( &t );
    spdlog::info( "The current date/time is " + string( asctime( timeinfo ) ) );
    auto now = ::time( nullptr );
    m_sleepDeadline = now + SLEEP_BETWEEN_PINGS;
}

/// Automatically called when a connection is first established.
void ClientBrain::nextValidId( OrderId orderId )
{
    spdlog::info( "Next Valid Id: " + to_string( orderId ) );
    *p_OrderId = orderId;
}

/// Automatically called when a connection is first established.
void ClientBrain::managedAccounts( const std::string& accountsList )
{
    accountID = accountsList; // this list is always a single ID for now
}

/// @brief Triggered whenever an error or update is thrown from the Host.
///
/// For example, when a market data error has occurred, this function is called.
/// Also, this serves as a callback to whenever subscriptions are cancelled.
void ClientBrain::error( int id, int errorCode, const std::string& errorString )
{
    spdlog::error( "Error: ID " + to_string( id ) + " Code " + to_string( errorCode ) + " MSG " + errorString );
    if( inter )
    {
        disconnect();
        exit( -1 );
    }
    else if( errorCode == 322 )
    {
        // more than 50 data requests at once, so this vectorId won't be answered
        openHistRequests.erase( (long)id );
    }
    else if( errorCode == 504 )
    {
        // just made a request with p_Client when disconnected
        *p_State = State::DISCONNECTED;
    }
    else if( errorCode == 201 )
    {
        // just submitted an order in which we didn't have the adequate funds to
        // cover it
        long search = (long)id;
        if( pendingOrders.find( search ) != pendingOrders.end() )
        {
            pendingOrders.erase( pendingOrders.find( search ) );
        }
        if( openOrders.find( search ) != openOrders.end() )
        {
            openOrders.erase( openOrders.find( search ) );
        }
    }
}

/// Unneeded for now.
void ClientBrain::winError( const std::string& str, int lastError )
{
    spdlog::error( "Error: Message " + str + " Code " + to_string( lastError ) );
}

/**
 * Account functions
 */
void ClientBrain::accountInit()
{
    p_Client->reqAccountSummary( accReqId, "All", AllAccountTags );
    std::this_thread::sleep_for( std::chrono::milliseconds( WAIT_TIME ) );
    p_Client->reqAccountUpdates( true, accountID );
    std::this_thread::sleep_for( std::chrono::milliseconds( WAIT_TIME ) );
}

void ClientBrain::closeSubscriptions()
{
    *p_State = State::ACCOUNTCLOSE;
    p_Client->cancelAccountSummary( accReqId );
    p_Client->reqAccountUpdates( false, accountID );
    *p_State = State::ACCOUNTCLOSESUCCESS;
}

void ClientBrain::portfolioUpdate( const TradeBase::Position& newPosition )
{
    auto samePos = positions.find( newPosition.getContract()->conId );
    if( samePos != positions.end() )
    {
        auto* pos = *samePos;
        if( newPosition.getContract()->conId == pos->getContract()->conId )
        {
            //auto newExec = newPosition.getLastExecution();
            //pos->updateQueue( newExec );
        }
    }
    else
    {
        positions.insert( new TradeBase::Position( newPosition ) );
    }
}

/** Callbacks for account
 *
 */

/// Callback for reqAccountSummary for all account related information
void ClientBrain::accountSummary( int reqId, const std::string& account, const std::string& tag, const std::string& value, const std::string& currency )
{
    accountID = account;
    cash = stof( value );
    accountCurrency = currency;
    spdlog::info( "Account information now is: ID " + accountID + " tag " + tag + " cash $" + to_string( cash ) + " Currency " + accountCurrency );
}

/// @brief Called whenever a position or account value changes, or every 3 minutes.
///
/// This callback is triggered every time an account update from the reqAccountUpdates subscription is received.
/// These account updates occur either when a position or account value is updated, or at most every 3 minutes.
void ClientBrain::updateAccountValue( const std::string& key, const std::string& val, const std::string& currency, const std::string& accountName )
{
    // spdlog::info( " Account Value Update: Key: " + key + ", Value: " + val + ", Currency: " + currency + ", Account: " + accountName );
    if( key == "CashBalance" )
    {
        cash = atof( val.data() );
    }
    else if( key == "RealizedPnL" )
    {
        PnL = atof( val.data() );
    }
    else if( key == "UnrealizedPnL" )
    {
        UPnL = atof( val.data() );
    }
}

/// @brief Called whenever a position in the subscribed account changes.
///
/// This callback is triggered every time an account update from the reqAccountUpdates subscription is received.
/// These account updates occur either when a position or account value is updated, or at most every 3 minutes.
void ClientBrain::updatePortfolio( const Contract& contract, double position, double marketPrice, double marketValue, double averageCost, double unrealizedPNL, double realizedPNL, const std::string& accountName )
{
    // if we're initializing the bot
    if( *p_State == State::INIT )
    {
        // initialize pre-existing positions from the incoming info, including synthetic order and execution objects
        auto order = Order();
        order.account = accountID;
        order.action = position > 0 ? "BUY" : "SELL";
        order.clientId = clientID;
        order.filledQuantity = position;
        auto exec = Execution();
        exec.acctNumber = accountID;
        exec.avgPrice = averageCost;
        exec.clientId = clientID;
        exec.cumQty = position;
        exec.price = averageCost;
        exec.shares = position;
        exec.side = position > 0 ? "BUY" : "SELL";
        exec.time = TradeBase::TimeStamp().toString();
        //auto pos = TradeBase::Position( contract, order, exec );
        //portfolioUpdate( pos );
        spdlog::info( "Updating portfolio: " + contract.symbol + ", size: " + to_string( position ) + ", price per share: $" + to_string( averageCost ) );
    }
}

/// @brief One of three callbacks from account updates. Receives the timestamp of the last account update.
///
/// This callback is triggered every time an account update from the reqAccountUpdates subscription is received.
/// These account updates occur either when a position or account value is updated, or at most every 3 minutes.
void ClientBrain::updateAccountTime( const std::string& timeStamp )
{
    spdlog::info( "The last account update was " + timeStamp );
}

/// Notifies when all account information has been received after subscribing to account updates.
void ClientBrain::accountDownloadEnd( const std::string& accountName )
{
    spdlog::info( "Account download ended for " + accountName );
}

void ClientBrain::position( const std::string& account, const Contract& contract, double position, double avgCost )
{
    // Compare the positions coming in to the positions in the account
    auto search = positions.find( contract.conId );
    if( search != positions.end() )
    {
        const auto* pos = *( search );
        if( pos->getAvgPrice() != avgCost )
        {
            spdlog::error( "Position with symbol " + pos->getContract()->symbol + " secType " + pos->getContract()->secType + " had an inaccuracte average cost!" );
        }
        if( pos->getPositionSize() != position )
        {
            spdlog::error( "Position with symbol " + pos->getContract()->symbol + " secType " + pos->getContract()->secType + " had an inaccuracte position size!" );
        }
    }
}

void ClientBrain::positionEnd()
{
    spdlog::info( "End of position update" );
}

/** 
 * Functions for Data
 */

void ClientBrain::dataInit()
{
    initContractVectors();
    startLiveData();
}

void ClientBrain::startLiveData()
{
    // market data lines
    /*for( auto& con : stockContracts )
    {
        newLiveRequest( con, getNextVectorId() );
        this_thread::sleep_for( chrono::milliseconds( INIT_WAIT_TIME ) );
    }*/
    // for some reason this causes the connection in the optimized version to break but not the debug version
    /*for( auto& con : optionContracts )
    {
        newLiveRequest( con, getNextVectorId() );
        this_thread::sleep_for( chrono::milliseconds( INIT_WAIT_TIME ) );
    }*/
    for( auto& con : futureContracts )
    {
        newLiveRequest( con, getNextVectorId() );
        this_thread::sleep_for( chrono::milliseconds( INIT_WAIT_TIME ) );
    }
}

void ClientBrain::harvest( int index )
{
    // historical data requests
    if( index == 0 )
    {
        for( auto& con : stockContracts )
        {
            spdlog::info( "Requesting historical data of index 0 for " + con.symbol );
            newHistRequest( con, getNextVectorId(), "1800 S", "1 secs", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
            newHistRequest( con, getNextVectorId(), "3600 S", "5 secs", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
            newHistRequest( con, getNextVectorId(), "14400 S", "10 secs", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
            newHistRequest( con, getNextVectorId(), "14400 S", "15 secs", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
            newHistRequest( con, getNextVectorId(), "28800 S", "30 secs", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
            newHistRequest( con, getNextVectorId(), "1 D", "1 min", "TRADES" ); // the length of all candles north of 30s have no length restrictions anymore
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
        }
        for( auto& con : futureContracts )
        {
            spdlog::info( "Requesting historical data of index 0 for " + con.symbol );
            newHistRequest( con, getNextVectorId(), "1800 S", "1 secs", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
            newHistRequest( con, getNextVectorId(), "3600 S", "5 secs", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
            newHistRequest( con, getNextVectorId(), "14400 S", "10 secs", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
            newHistRequest( con, getNextVectorId(), "14400 S", "15 secs", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
            newHistRequest( con, getNextVectorId(), "28800 S", "30 secs", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
            newHistRequest( con, getNextVectorId(), "1 D", "1 min", "TRADES" ); // the length of all candles north of 30s have no length restrictions anymore
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
        }
        *p_State = State::DATAHARVEST_TIMEOUT_0;
        startTimer();
    }
    else if( index == 1 )
    {
        for( auto& con : stockContracts )
        {
            spdlog::info( "Requesting historical data of index 1 for " + con.symbol );
            newHistRequest( con, getNextVectorId(), "2 D", "2 mins", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
            newHistRequest( con, getNextVectorId(), "1 W", "3 mins", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
            newHistRequest( con, getNextVectorId(), "1 W", "5 mins", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
            newHistRequest( con, getNextVectorId(), "1 W", "10 mins", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
            newHistRequest( con, getNextVectorId(), "1 W", "15 mins", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
            newHistRequest( con, getNextVectorId(), "1 W", "20 mins", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
        }
        for( auto& con : futureContracts )
        {
            spdlog::info( "Requesting historical data of index 1 for " + con.symbol );
            newHistRequest( con, getNextVectorId(), "2 D", "2 mins", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
            newHistRequest( con, getNextVectorId(), "1 W", "3 mins", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
            newHistRequest( con, getNextVectorId(), "1 W", "5 mins", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
            newHistRequest( con, getNextVectorId(), "1 W", "10 mins", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
            newHistRequest( con, getNextVectorId(), "1 W", "15 mins", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
            newHistRequest( con, getNextVectorId(), "1 W", "20 mins", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
        }
        *p_State = State::DATAHARVEST_TIMEOUT_1;
        startTimer();
    }
    else if( index == 2 )
    {
        for( auto& con : stockContracts )
        {
            spdlog::info( "Requesting historical data of index 2 for " + con.symbol );
            newHistRequest( con, getNextVectorId(), "1 M", "30 mins", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
            newHistRequest( con, getNextVectorId(), "1 M", "1 hour", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
            newHistRequest( con, getNextVectorId(), "1 M", "2 hours", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
            newHistRequest( con, getNextVectorId(), "1 M", "3 hours", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
            newHistRequest( con, getNextVectorId(), "1 M", "4 hours", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
            newHistRequest( con, getNextVectorId(), "1 M", "8 hours", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
        }
        for( auto& con : futureContracts )
        {
            spdlog::info( "Requesting historical data of index 1 for " + con.symbol );
            newHistRequest( con, getNextVectorId(), "2 D", "2 mins", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
            newHistRequest( con, getNextVectorId(), "1 W", "3 mins", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
            newHistRequest( con, getNextVectorId(), "1 W", "5 mins", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
            newHistRequest( con, getNextVectorId(), "1 W", "10 mins", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
            newHistRequest( con, getNextVectorId(), "1 W", "15 mins", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
            newHistRequest( con, getNextVectorId(), "1 W", "20 mins", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
        }
        *p_State = State::DATAHARVEST_TIMEOUT_2;
        startTimer();
    }
    else if( index == 3 )
    {
        for( auto& con : stockContracts )
        {
            spdlog::info( "Requesting historical data of index 3 for " + con.symbol );
            newHistRequest( con, getNextVectorId(), "1 Y", "1 day", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
        }
        for( auto& con : futureContracts )
        {
            spdlog::info( "Requesting historical data of index 3 for " + con.symbol );
            newHistRequest( con, getNextVectorId(), "1 Y", "1 day", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( REQ_WAIT_TIME ) );
        }
        *p_State = State::DATAHARVEST_LIVE;
    }
}

void ClientBrain::newLiveRequest( Contract& con, long vecId )
{
    auto newVec = make_shared<TradeBase::DataArray>( vecId, con.conId, con.symbol, con.secId, con.secType, con.exchange, con.currency );
    openDataLines.insert( vecId );
    if( con.secType == "STK" )
    {
        snapMap[vecId] = SnapHold();
    }
    else if( con.secType == "OPT" )
    {
        newVec->contract.lastTradeDateOrContractMonth = con.lastTradeDateOrContractMonth;
        newVec->contract.strike = con.strike;
        newVec->contract.right = con.right;
        newVec->exprDate = TradeBase::TimeStamp( con.lastTradeDateOrContractMonth, true );
        optionMap[vecId] = OptionHold();
    }
    else if( con.secType == "FUT" )
    {
        newVec->contract.lastTradeDateOrContractMonth = con.lastTradeDateOrContractMonth;
        newVec->exprDate = TradeBase::TimeStamp( con.lastTradeDateOrContractMonth, true );
        futureMap[vecId] = FutureHold();
    }
    DataArrays.insert( newVec );
    spdlog::info( "Requesting live data stream for " + con.symbol );
    p_Client->reqMktData( vecId, con, "", false, false, TagValueListSPtr() );
}

void ClientBrain::newHistRequest( Contract& con, long vecId, const string& length, const string& barlength, const string& type )
{
    auto   newVec = make_shared<TradeBase::DataArray>( vecId, con.conId, con.symbol, con.secId, con.secType, con.exchange, con.currency );
    string inter = barlength;
    inter.erase( remove_if( inter.begin(), inter.end(), []( unsigned char c ) { return std::isspace( c ); } ), inter.end() );
    newVec->interval = inter;
    DataArrays.insert( newVec );
    openHistRequests.insert( vecId );
    p_Client->reqHistoricalData( vecId, con, "", length, barlength, type, 1, 1, false, TagValueListSPtr() );
}

void ClientBrain::startTimer()
{
    start = chrono::high_resolution_clock::now();
}

bool ClientBrain::checkTimer()
{
    auto nowTime = chrono::_V2::system_clock::now();
    auto intDuration = chrono::duration_cast<chrono::seconds>( nowTime - start );
    spdlog::info( "Seconds until next data harvest: " + to_string( TIMEOUT - intDuration.count() ) );
    return chrono::duration_cast<chrono::seconds>( chrono::_V2::system_clock::now() - start ).count() > TIMEOUT;
}

void ClientBrain::updateCandle( TickerId reqId, const Bar& bar )
{
    auto point = DataArrays.find( reqId );
    if( point != DataArrays.end() )
    {
        TradeBase::CandleStruct newPoint;
        newPoint.time = TradeBase::TimeStamp( bar.time );
        newPoint.open = bar.open;
        newPoint.high = bar.high;
        newPoint.low = bar.low;
        newPoint.close = bar.close;
        newPoint.volume = bar.volume;
        point->get()->addPoint( newPoint );
    }
    else
    {
        spdlog::error( "Couldn't find a vector matching this request Id..." );
    }
}

void ClientBrain::updatePrice( TickerId reqId, TradeBase::SnapStruct& newPoint, double price, int field )
{
    if( field == 1 )
    {
        newPoint.bidPrice = price;
    }
    else if( field == 2 )
    {
        newPoint.askPrice = price;
    }
    else if( field == 4 )
    {
        // last trade price
        newPoint.askPrice = price;
        newPoint.bidPrice = price;
    }
    if( newPoint.valid() )
    {
        // spdlog::info( "New snap struct " + newPoint.toString() );
        auto point = DataArrays.find( reqId );
        if( point != DataArrays.end() )
        {
            point->get()->addPoint( newPoint );
        }
        else
        {
            spdlog::error( "Couldn't find a vector matching this request Id..." );
        }
        newPoint.clear();
    }
}

void ClientBrain::updatePrice( TickerId reqId, TradeBase::OptionStruct& newPoint, double price, int field )
{
    if( field == 1 )
    {
        newPoint.bidPrice = price;
    }
    else if( field == 2 )
    {
        newPoint.askPrice = price;
    }
    else if( field == 4 )
    {
        // last trade price
        newPoint.askPrice = price;
        newPoint.bidPrice = price;
    }
    if( newPoint.valid() )
    {
        // spdlog::info( "New option struct " + newPoint.toString() );
        auto point = DataArrays.find( reqId );
        if( point != DataArrays.end() )
        {
            point->get()->addPoint( newPoint );
        }
        else
        {
            spdlog::error( "Couldn't find a vector matching this request Id..." );
        }
        newPoint.clear();
    }
}

void ClientBrain::updateSize( TickerId reqId, TradeBase::SnapStruct& newPoint, int value, int field )
{
    if( field == 0 )
    {
        newPoint.bidSize = value;
    }
    else if( field == 3 )
    {
        newPoint.askSize = value;
    }
    else if( field == 5 )
    {
        // last trade
        newPoint.askSize = value;
        newPoint.bidSize = value;
    }
    if( newPoint.valid() )
    {
        addPoint( reqId, newPoint );
    }
}

void ClientBrain::updateSize( TickerId reqId, TradeBase::OptionStruct& newPoint, int value, int field )
{
    if( field == 0 )
    {
        newPoint.bidSize = value;
    }
    else if( field == 3 )
    {
        newPoint.askSize = value;
    }
    else if( field == 5 )
    {
        // last trade
        newPoint.askSize = value;
        newPoint.bidSize = value;
    }
    if( newPoint.valid() )
    {
        // spdlog::info( "New option struct " + newPoint.toString() );
        auto point = DataArrays.find( reqId );
        if( point != DataArrays.end() )
        {
            point->get()->addPoint( newPoint );
        }
        else
        {
            spdlog::error( "Couldn't find a vector matching this request Id..." );
        }
        newPoint.clear();
    }
}

void ClientBrain::updateOptionGreeks( TickerId reqId, TradeBase::OptionStruct& newPoint, double impliedVol, double delta, double optPrice, double pvDividend, double gamma, double vega, double theta, int field )
{
    // sometimes (especially at the beginning) the data can be crap, so make sure
    // the values are sane
    if( abs( delta ) > 1 || abs( gamma ) > 1 )
    {
        spdlog::warn( "The greeks received are garbage values." );
        return;
    }
    // bid computation
    if( field == 10 )
    {
        newPoint.bidImpliedVol = impliedVol;
        newPoint.bidDelta = delta;
        newPoint.bidPrice = optPrice;
        newPoint.bidPvDividend = pvDividend;
        newPoint.bidGamma = gamma;
        newPoint.bidVega = vega;
        newPoint.bidTheta = theta;
    }
    // ask computation
    else if( field == 11 )
    {
        newPoint.askImpliedVol = impliedVol;
        newPoint.askDelta = delta;
        newPoint.askPrice = optPrice;
        newPoint.askPvDividend = pvDividend;
        newPoint.askGamma = gamma;
        newPoint.askVega = vega;
        newPoint.askTheta = theta;
    }
    // last execution computation
    else if( field == 12 )
    {
        newPoint.bidImpliedVol = impliedVol;
        newPoint.bidDelta = delta;
        newPoint.bidPrice = optPrice;
        newPoint.bidPvDividend = pvDividend;
        newPoint.bidGamma = gamma;
        newPoint.bidVega = vega;
        newPoint.bidTheta = theta;
        newPoint.askImpliedVol = impliedVol;
        newPoint.askDelta = delta;
        newPoint.askPrice = optPrice;
        newPoint.askPvDividend = pvDividend;
        newPoint.askGamma = gamma;
        newPoint.askVega = vega;
        newPoint.askTheta = theta;
    }
    if( newPoint.valid() )
    {
        addPoint( reqId, newPoint );
    }
}

void ClientBrain::addPoint( long reqId, TradeBase::SnapStruct newPoint )
{
    // spdlog::info( "New snap struct " + newPoint.toString() );
    auto point = DataArrays.find( reqId );
    if( point != DataArrays.end() )
    {
        point->get()->addPoint( newPoint );
    }
    else
    {
        spdlog::error( "Couldn't find a vector matching this request Id..." );
        return;
    }
    updateTimeLine( *point );
    newPoint.clear();
}

void ClientBrain::addPoint( long reqId, TradeBase::OptionStruct newPoint )
{
    // spdlog::info( "New option struct " + newPoint.toString() );
    auto point = DataArrays.find( reqId );
    if( point != DataArrays.end() )
    {
        point->get()->addPoint( newPoint );
    }
    else
    {
        spdlog::error( "Couldn't find a vector matching this request Id..." );
        return;
    }
    updateTimeLine( *point );
    newPoint.clear();
}

void ClientBrain::updateTimeLine( const shared_ptr<TradeBase::DataArray>& vec )
{
    auto last = vec->getLastPoint();
    // TODO: this is not allowed, the last iterator is invalid
    if( last )
    {
        auto it = *last;
        if( TimeLine.find( it->getTime() ) == TimeLine.end() )
        {
            TimeLine[it->getTime()] = TradeBase::GlobalTimePoint( vec, it );
            currentDataPoint = prev( TimeLine.end() );
        }
        else
        {
            TimeLine[it->getTime()].addVector( vec, it );
        }
    }
    updatedLines.insert( vec->vectorId );
}

void ClientBrain::initContractVectors()
{
    // stock tickers
    /*
    Contract MSFT = Contract();
    MSFT.conId = 272093;
    MSFT.symbol = "MSFT";
    MSFT.secType = "STK";
    MSFT.currency = "USD";
    MSFT.exchange = "SMART";
    MSFT.primaryExchange = "NASDAQ";
    stockContracts.push_back( MSFT );

    Contract AAPL = Contract();
    AAPL.conId = 265598;
    AAPL.symbol = "AAPL";
    AAPL.secType = "STK";
    AAPL.currency = "USD";
    AAPL.exchange = "SMART";
    AAPL.primaryExchange = "NASDAQ";
    stockContracts.push_back( AAPL );

    Contract NFLX = Contract();
    NFLX.conId = 15124833;
    NFLX.symbol = "NFLX";
    NFLX.secType = "STK";
    NFLX.currency = "USD";
    NFLX.exchange = "SMART";
    NFLX.primaryExchange = "NASDAQ";
    stockContracts.push_back( NFLX );

    Contract AMZN = Contract();
    AMZN.conId = 3691937;
    AMZN.symbol = "AMZN";
    AMZN.secType = "STK";
    AMZN.currency = "USD";
    AMZN.exchange = "SMART";
    AMZN.primaryExchange = "NASDAQ";
    stockContracts.push_back( AMZN );

    Contract GOOG = Contract();
    GOOG.conId = 208813720;
    GOOG.symbol = "GOOG";
    GOOG.secType = "STK";
    GOOG.currency = "USD";
    GOOG.exchange = "SMART";
    GOOG.primaryExchange = "NASDAQ";
    stockContracts.push_back( GOOG );

    Contract TSLA = Contract();
    TSLA.conId = 76792991;
    TSLA.symbol = "TSLA";
    TSLA.secType = "STK";
    TSLA.currency = "USD";
    TSLA.exchange = "SMART";
    TSLA.primaryExchange = "NASDAQ";
    stockContracts.push_back( TSLA );

    Contract BA = Contract();
    BA.conId = 4762;
    BA.symbol = "BA";
    BA.secType = "STK";
    BA.currency = "USD";
    BA.exchange = "SMART";
    BA.primaryExchange = "NYSE";
    stockContracts.push_back( BA );

    Contract INTC = Contract();
    INTC.conId = 270639;
    INTC.symbol = "INTC";
    INTC.secType = "STK";
    INTC.currency = "USD";
    INTC.exchange = "SMART";
    INTC.primaryExchange = "NASDAQ";
    stockContracts.push_back( INTC );

    Contract NVDA = Contract();
    NVDA.conId = 4815747;
    NVDA.symbol = "NVDA";
    NVDA.secType = "STK";
    NVDA.currency = "USD";
    NVDA.exchange = "SMART";
    NVDA.primaryExchange = "NASDAQ";
    stockContracts.push_back( NVDA );

    Contract FB = Contract();
    FB.conId = 107113386;
    FB.symbol = "FB";
    FB.secType = "STK";
    FB.currency = "USD";
    FB.exchange = "SMART";
    FB.primaryExchange = "NASDAQ";
    stockContracts.push_back( FB );

    // options for AMZN
    Contract OPT0 = Contract();
    OPT0.conId = getNextConId();
    OPT0.symbol = "AMZN";
    OPT0.secType = "OPT";
    OPT0.exchange = "AMEX";
    OPT0.currency = "USD";
    OPT0.lastTradeDateOrContractMonth = "20210716";
    OPT0.strike = 3050;
    OPT0.right = "C";
    OPT0.multiplier = "100";
    optionContracts.push_back( OPT0 );

    Contract OPT1 = Contract( OPT0 );
    OPT1.conId = getNextConId();
    OPT1.strike = 3100;
    optionContracts.push_back( OPT1 );
    Contract OPT2 = Contract( OPT0 );
    OPT2.conId = getNextConId();
    OPT2.strike = 3150;
    optionContracts.push_back( OPT2 );
    Contract OPT3 = Contract( OPT0 );
    OPT3.conId = getNextConId();
    OPT3.strike = 3150;
    OPT3.right = "P";
    optionContracts.push_back( OPT3 );
    Contract OPT4 = Contract( OPT3 );
    OPT4.conId = getNextConId();
    OPT4.strike = 3100;
    optionContracts.push_back( OPT4 );
    Contract OPT5 = Contract( OPT3 );
    OPT5.conId = getNextConId();
    OPT5.strike = 3050;
    optionContracts.push_back( OPT5 );

    // options for TSLA
    Contract OPT6 = Contract();
    OPT6.conId = getNextConId();
    OPT6.symbol = "TSLA";
    OPT6.secType = "OPT";
    OPT6.exchange = "AMEX";
    OPT6.currency = "USD";
    OPT6.lastTradeDateOrContractMonth = "20210716";
    OPT6.strike = 1400;
    OPT6.right = "C";
    OPT6.multiplier = "100";
    optionContracts.push_back( OPT6 );

    Contract OPT7 = Contract( OPT6 );
    OPT7.conId = getNextConId();
    OPT7.strike = 1450;
    optionContracts.push_back( OPT7 );
    Contract OPT8 = Contract( OPT6 );
    OPT8.conId = getNextConId();
    OPT8.strike = 1500;
    optionContracts.push_back( OPT8 );
    Contract OPT9 = Contract( OPT6 );
    OPT9.conId = getNextConId();
    OPT9.strike = 1500;
    OPT9.right = "P";
    optionContracts.push_back( OPT9 );
    Contract OPT10 = Contract( OPT9 );
    OPT10.conId = getNextConId();
    OPT10.strike = 1450;
    optionContracts.push_back( OPT10 );
    Contract OPT11 = Contract( OPT9 );
    OPT11.conId = getNextConId();
    OPT11.strike = 1400;
    optionContracts.push_back( OPT11 );
    */
    // Futures
    Contract FUT0 = Contract();
    //FUT0.conId = 	428520022;//getNextConId();
    FUT0.symbol = "MES";
    FUT0.localSymbol = "MESU1";
    FUT0.secType = "FUT";
    FUT0.exchange = "SMART";
    FUT0.primaryExchange = "GLOBEX";
    FUT0.currency = "USD";
    // because we define the local symbol, this is redundant
    //FUT0.lastTradeDateOrContractMonth = "20210917";
    //FUT0.multiplier = "5";
    futureContracts.push_back( FUT0 );
    Contract FUT1 = Contract();
    //FUT0.conId = 	428520022;//getNextConId();
    FUT1.symbol = "MNQ";
    FUT1.localSymbol = "MNQU1";
    FUT1.secType = "FUT";
    FUT1.exchange = "SMART";
    FUT1.primaryExchange = "GLOBEX";
    FUT1.currency = "USD";
    // because we define the local symbol, this is redundant
    //FUT1.lastTradeDateOrContractMonth = "20210917";
    //FUT1.multiplier = "2";
    futureContracts.push_back( FUT1 );
}

void ClientBrain::printCSVs()
{
    for( const auto& vec : DataArrays )
    {
        // print each array to CSV
        ofstream output;
        string   fileString = "symbol:" + vec->contract.symbol + "\n";
        fileString += "secId:" + vec->contract.secId + "\n";
        fileString += "secType:" + vec->contract.secType + "\n";
        fileString += "exchange:" + vec->contract.exchange + "\n";
        fileString += "currency:" + vec->contract.currency + "\n";
        fileString += "conId:" + to_string( vec->contract.conId ) + "\n";
        if( vec->array.begin()->candles() )
        {
            fileString += "interval: " + vec->interval + "\n";
            fileString += "timestamp,open,high,low,close,volume\n";
            for( const auto& p : vec->array )
            {
                fileString += p.toString() + "\n";
            }
            if( vec->array.size() > 1 )
            {
                output.open( vec->contract.symbol + "_candle_" + vec->getStart( true ) + "_" + vec->getEnd( true ) + "_" + vec->interval + "_" + ".csv" );
                output.write( fileString.data(), fileString.size() );
            }
        }
        else if( vec->array.begin()->snaps() )
        {

            fileString += "timestamp,askPrice,askSize,bidPrice,bidSize\n";
            for( const auto& p : vec->array )
            {
                fileString += p.toString() + "\n";
            }
            if( vec->array.size() > 1 )
            {
                output.open( vec->contract.symbol + "_snap_" + vec->getStart( true ) + "_" + vec->getEnd( true ) + ".csv" );
                output.write( fileString.data(), fileString.size() );
            }
        }
        else if( vec->array.begin()->options() )
        {
            fileString += "strike:" + to_string( vec->contract.strike ) + "\n";
            fileString += "exprDate:" + vec->exprDate.toString() + "\n";
            fileString += "right:" + vec->contract.right + "\n";
            fileString += "timestamp,bidImpVol,bidDel,bidPrice,bidSize,bidPvDiv,bidGam,bidVeg,bidThet,askImpVol,askDel,askPrice,askSize,askPvDiv,askGam,askVeg,askThet\n";

            for( const auto& p : vec->array )
            {
                fileString += p.toString() + "\n";
            }
            if( vec->array.size() > 1 )
            {
                output.open( vec->contract.symbol + "_option_" + vec->getStart( true ) + "_" + vec->getEnd( true ) + "_" + vec->exprDate.toString( true ) + "_" + to_string( vec->contract.strike ) + "_" + vec->contract.right + ".csv" );
                output.write( fileString.data(), fileString.size() );
            }
        }
    }
}

bool ClientBrain::updated()
{
    if( !updatedLines.empty() )
    {
        updatedLines.clear();
        return true;
    }
    return false;
}

bool ClientBrain::updated() const
{
    return openDataLines == updatedLines;
}

/// Callbacks for data operations
void ClientBrain::tickPrice( TickerId tickerId, TickType field, double price, const TickAttrib& attribs )
{
    auto snapVec = snapMap.find( tickerId );
    auto opVec = optionMap.find( tickerId );
    auto futVec = futureMap.find( tickerId );

    if( snapVec != snapMap.end() )
    {
        if( field == 1 || field == 2 )
        {
            updatePrice( tickerId, snapVec->second.bidAsk, price, field );
        }
        else if( field == 4 )
        {
            updatePrice( tickerId, snapVec->second.lastTrade, price, field );
        }
    }
    else if( opVec != optionMap.end() )
    {
        if( field == 1 || field == 2 )
        {
            updatePrice( tickerId, opVec->second.bidAsk, price, field );
        }
        else if( field == 4 )
        {
            updatePrice( tickerId, opVec->second.lastTrade, price, field );
        }
    }
    else if( futVec != futureMap.end() )
    {
        if( field == 1 || field == 2 )
        {
            updatePrice( tickerId, futVec->second.bidAsk, price, field );
        }
        else if( field == 4 )
        {
            updatePrice( tickerId, futVec->second.lastTrade, price, field );
        }
    }
}

void ClientBrain::tickSize( TickerId tickerId, TickType field, int size )
{
    auto snapVec = snapMap.find( tickerId );
    auto opVec = optionMap.find( tickerId );
    auto futVec = futureMap.find( tickerId );
    if( snapVec != snapMap.end() )
    {
        if( field == 0 || field == 3 )
        {
            updateSize( tickerId, snapVec->second.bidAsk, size, field );
        }
        else if( field == 5 )
        {
            updateSize( tickerId, snapVec->second.lastTrade, size, field );
        }
    }
    else if( opVec != optionMap.end() )
    {
        if( field == 0 || field == 3 )
        {
            updateSize( tickerId, opVec->second.bidAsk, size, field );
        }
        else if( field == 5 )
        {
            updateSize( tickerId, opVec->second.lastTrade, size, field );
        }
    }
    else if( futVec != futureMap.end() )
    {
        if( field == 0 || field == 3 )
        {
            updateSize( tickerId, futVec->second.bidAsk, size, field );
        }
        else if( field == 5 )
        {
            updateSize( tickerId, futVec->second.lastTrade, size, field );
        }
    }
    else
    {
        spdlog::error( "Could not find an entry in the snapMap or optionMap for "
                       "data request " +
                       to_string( tickerId ) );
    }
}

void ClientBrain::tickOptionComputation( TickerId tickerId, TickType tickType, int tickAttrib, double impliedVol, double delta, double optPrice, double pvDividend, double gamma, double vega, double theta, double undPrice )
{
    auto opVec = optionMap.find( tickerId );
    if( opVec != optionMap.end() )
    {
        // undPrice is always crap, don't pass it
        if( tickType == 10 || tickType == 11 )
        {
            updateOptionGreeks( tickerId, opVec->second.bidAsk, impliedVol, delta, optPrice, pvDividend, gamma, vega, theta, tickType );
        }
        else if( tickType == 12 )
        {
            updateOptionGreeks( tickerId, opVec->second.lastTrade, impliedVol, delta, optPrice, pvDividend, gamma, vega, theta, tickType );
        }
    }
    else
    {
        spdlog::error( "Could not find an entry in the option map for data request " + to_string( tickerId ) );
    }
}

void ClientBrain::tickGeneric( TickerId tickerId, TickType tickType, double value )
{
    spdlog::info( "New value for " + to_string( tickerId ) + " of type " + to_string( tickType ) + " is " + to_string( value ) );
}

void ClientBrain::tickString( TickerId tickerId, TickType tickType, const std::string& value )
{
    // handles NBBO exchange info for snapshot callbacks
    // spdlog::info( "New value of type " + to_string( tickType ) + " for " +
    // to_string( tickerId ) + " is " + value );
}

void ClientBrain::tickSnapshotEnd( int reqId )
{
    spdlog::info( "Snapshot for " + to_string( reqId ) + " has ended." );
}

void ClientBrain::tickReqParams( int tickerId, double minTick, const std::string& bboExchange, int snapshotPermissions )
{
    spdlog::warn( "The market data permissions level for req " + to_string( tickerId ) + " is " + to_string( snapshotPermissions ) + " and the minTick and BBO exchange are " + to_string( minTick ) + " and " + bboExchange );
    m_bboExchange = bboExchange;
}

/// @brief Automatic callback from reqMktData() to specify the type of data
/// that will (or has already) come over the wire.
///
/// 1 -> Real-time market data
/// 2 -> "Frozen" data, or the data that was last recorded by IB before market close.
/// 3 -> Delayed market data by 15-20 minutes
/// 4 -> Delayed-frozen
void ClientBrain::marketDataType( TickerId reqId, int marketDataType )
{
    spdlog::info( "The Market Data being requested by " + to_string( reqId ) + " has been changed to type " + to_string( marketDataType ) );
}

void ClientBrain::historicalDataEnd( int reqId, const std::string& startDateStr, const std::string& endDateStr )
{
    spdlog::info( "HistoricalDataEnd. ReqId: " + to_string( reqId ) + " - Start Date: " + startDateStr + ", End Date: " + endDateStr );
    openHistRequests.erase( (long)reqId );
}

void ClientBrain::historicalTicks( int reqId, const std::vector<HistoricalTick>& ticks, bool done )
{
    for( const HistoricalTick& tick : ticks )
    {
        spdlog::info( "In historicalTicks callback." );
    }
}

void ClientBrain::historicalTicksBidAsk(
    int reqId, const std::vector<HistoricalTickBidAsk>& ticks, bool done )
{
    for( const HistoricalTickBidAsk& tick : ticks )
    {
        spdlog::info( "In historicalTicksBidAsk callback." );
    }
}

void ClientBrain::historicalTicksLast(
    int reqId, const std::vector<HistoricalTickLast>& ticks, bool done )
{
    for( const auto& tick : ticks )
    {
        spdlog::info( "In historicalTicksLast callback." );
    }
}

void ClientBrain::historicalData( TickerId reqId, const Bar& bar )
{
    updateCandle( reqId, bar );
}

void ClientBrain::historicalDataUpdate( TickerId reqId, const Bar& bar )
{
    updateCandle( reqId, bar );
}

/** 
 * Broker functions
 */
void ClientBrain::placeOrder( pair<Contract, Order>& p )
{
    auto contract = p.first;
    auto order = p.second;
    spdlog::info( "Placing order for symbol " + contract.symbol + ", SecType " + contract.secType + " with order ID " + to_string( *p_OrderId ) );
    auto newOrderId = *p_OrderId;
    orderMap[newOrderId] = p;
    ( *p_OrderId )++;
    order.orderId = newOrderId;
    openOrders.insert( p );

    p_Client->placeOrder( newOrderId, contract, order );
}

void ClientBrain::filledOrder( long reqId, const ExecutionFilter& filter )
{
    p_Client->reqExecutions( reqId, filter );
}

/*
 * Callbacks
 */

/// Gives up-to-date information about each order every time its state changes. Callback from EClientSocket::placeOrder
void ClientBrain::orderStatus( OrderId orderId, const std::string& status, double filled, double remaining, double avgFillPrice, int permId, int parentId, double lastFillPrice, int clientId, const std::string& whyHeld, double mktCapPrice )
{
    spdlog::warn( "In orderStatus. The status message is " + status );
    if( status == "ApiPending" )
    {
        // yet to be submitted to IB server, consider it open
        spdlog::info( "Received ApiPending status for order " + to_string( orderId ) );
        pendingOrders.insert( orderMap[orderId] );
    }
    else if( status == "PendingSubmit" )
    {
        // no confirmation from IB that the order has been received, consider open
        spdlog::info( "Received PendingSubmit status for order " + to_string( orderId ) );
        pendingOrders.insert( orderMap[orderId] );
    }
    else if( status == "PendingCancel" )
    {
        spdlog::info( "Received PendingCancel status for order " + to_string( orderId ) );
        // currently pending cancel, not handled for now
    }
    else if( status == "PreSubmitted" )
    {
        // order has been accepted by IB and is yet to be "elected"
        spdlog::info( "Received PreSubmitted status for order " + to_string( orderId ) );
        pendingOrders.insert( orderMap[orderId] );
    }
    else if( status == "Submitted" )
    {
        // order has been accepted by the system
        spdlog::info( "Received Submitted status for order " + to_string( orderId ) );
        pendingOrders.erase( orderMap[orderId] );
        openOrders.insert( orderMap[orderId] );
    }
    else if( status == "ApiCancelled" )
    {
        // order has been requested to be cancelled, after being accepted but before
        // being acknowledged
        spdlog::info( "Received ApiCancelled status for order " +
                      to_string( orderId ) );
        pendingOrders.erase( orderMap[orderId] );
        openOrders.erase( orderMap[orderId] );
    }
    else if( status == "Cancelled" )
    {
        // order has been confirmed to be cancelled
        spdlog::info( "Received Cancelled status for order " + to_string( orderId ) );
        openOrders.erase( orderMap[orderId] );
    }
    else if( status == "Filled" )
    {
        // order has been completely filled
        spdlog::info( "Received Filled status for order " + to_string( orderId ) );
        auto filter = ExecutionFilter();
        filter.m_acctCode = accountID;
        filter.m_clientId = clientId;
        filter.m_symbol = orderMap[orderId].first.symbol;
        filter.m_secType = orderMap[orderId].first.secType;
        filter.m_side = orderMap[orderId].second.action;
        auto newExecId = getNextReqId();
        executionMap[newExecId] = orderMap[orderId];
        filledOrder( newExecId, filter );
        openOrders.erase( orderMap[orderId] );
    }
    else if( status == "Inactive" )
    {
        // order has been received by the system but is no longer active due to
        // either rejection or cancellation
        spdlog::info( "Received Inactive status for order " + to_string( orderId ) );
        openOrders.erase( orderMap[orderId] );
    }
}

/// Receives all open orders' information. Callback from EClientSocket::reqAllOpenOrders. Does not create a subscription to open order updates
void ClientBrain::openOrder( OrderId orderId, const Contract& contract, const Order& order, const OrderState& orderState )
{
    spdlog::info( "Received openOrder info for order " + to_string( order.orderId ) + " of symbol " + contract.symbol );
}

/// Notifies end of open order updates
void ClientBrain::openOrderEnd()
{
    spdlog::info( "End openOrders" );
}

/// Receives all completed orders. Callback from EClientSocket::reqCompletedOrders
void ClientBrain::completedOrder( const Contract& contract, const Order& order, const OrderState& orderState )
{
    spdlog::info( "Received completed order update for orderId " + to_string( order.orderId ) + " of symbol " + contract.symbol + " with size " + to_string( order.totalQuantity ) );
}

/// Notifies end of completed order updates
void ClientBrain::completedOrdersEnd() { spdlog::info( "End completedOrders" ); }

void ClientBrain::execDetails( int reqId, const Contract& contract, const Execution& execution )
{
    if( executionMap.find( reqId ) != executionMap.end() )
    {
        auto pa = executionMap[reqId];
        spdlog::info( "Received executions targeted at orders for position with symbol " + pa.first.symbol + ", secId: " + pa.first.secType );
        //update( Position( pa.first, pa.second, execution ) );
    }
    else
    {
        spdlog::error( "Received a reqId in execDetails that did not map to a position!" );
    }
}

void ClientBrain::execDetailsEnd( int reqId )
{
    spdlog::info( "End execDetails for " + to_string( reqId ) );
}

/* Stuff we really don't care about but is required because of the TWS API */

void ClientBrain::newsProviders( const std::vector<NewsProvider>& newsProviders )
{
    printf( "News providers (%lu):\n", newsProviders.size() );

    for( unsigned int i = 0; i < newsProviders.size(); i++ )
    {
        printf( "News provider [%d] - providerCode: %s providerName: %s\n", i,
                newsProviders[i].providerCode.c_str(),
                newsProviders[i].providerName.c_str() );
    }
}

void ClientBrain::updateMktDepth( TickerId id, int position, int operation, int side,
                                  double price, int size )
{
    printf( "UpdateMarketDepth. %ld - Position: %d, Operation: %d, Side: %d, "
            "Price: %g, Size: %d\n",
            id, position, operation, side, price, size );
}

void ClientBrain::bondContractDetails( int                    reqId,
                                       const ContractDetails& contractDetails )
{
    printf( "BondContractDetails begin. ReqId: %d\n", reqId );
    // printBondContractDetailsMsg( contractDetails );
    printf( "BondContractDetails end. ReqId: %d\n", reqId );
}

void ClientBrain::updateMktDepthL2( TickerId id, int position,
                                    const std::string& marketMaker, int operation,
                                    int side, double price, int size,
                                    bool isSmartDepth )
{
    printf( "UpdateMarketDepthL2. %ld - Position: %d, Operation: %d, Side: %d, "
            "Price: %g, Size: %d, isSmartDepth: %d\n",
            id, position, operation, side, price, size, (int)isSmartDepth );
}

void ClientBrain::positionMulti( int reqId, const std::string& account,
                                 const std::string& modelCode,
                                 const Contract& contract, double pos,
                                 double avgCost )
{
    printf( "Position Multi. Request: %d, Account: %s, ModelCode: %s, Symbol: %s, "
            "SecType: %s, Currency: %s, Position: %g, Avg Cost: %g\n",
            reqId, account.c_str(), modelCode.c_str(), contract.symbol.c_str(),
            contract.secType.c_str(), contract.currency.c_str(), pos, avgCost );
}

void ClientBrain::positionMultiEnd( int reqId )
{
    printf( "Position Multi End. Request: %d\n", reqId );
}

void ClientBrain::pnl( int reqId, double dailyPnL, double unrealizedPnL,
                       double realizedPnL )
{
    printf(
        "PnL. ReqId: %d, daily PnL: %g, unrealized PnL: %g, realized PnL: %g\n",
        reqId, dailyPnL, unrealizedPnL, realizedPnL );
}

void ClientBrain::pnlSingle( int reqId, int pos, double dailyPnL,
                             double unrealizedPnL, double realizedPnL, double value )
{
    printf( "PnL Single. ReqId: %d, pos: %d, daily PnL: %g, unrealized PnL: %g, "
            "realized PnL: %g, value: %g\n",
            reqId, pos, dailyPnL, unrealizedPnL, realizedPnL, value );
}

/** Functions useful for TBData
 * Market data streaming
 * Real time bars
 * Historical data
 * Tick-by-tick
 * Market depth
 * Market data type settings
 * Symbol searching
 * Market scanning
 * Histogram data
 * Tick data callbacks
 * Options chain requests and callback
 */

void ClientBrain::tickDataOperation()
{
    /*
    std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
    p_Client->reqMktData( 1001, ContractSamples::StockComboContract(), "", false,
                            false, TagValueListSPtr() );
    p_Client->reqMktData( 1002, ContractSamples::OptionWithLocalSymbol(), "",
                            false, false, TagValueListSPtr() );
    p_Client->reqMktData( 1003, ContractSamples::FutureComboContract(), "", true,
                            false, TagValueListSPtr() );
    p_Client->reqMktData( 1004, ContractSamples::USStockAtSmart(), "233,236,258",
                            false, false, TagValueListSPtr() );
    p_Client->reqMktData( 1005, ContractSamples::USStock(), "mdoff,292:BZ", false,
                            false, TagValueListSPtr() );
    p_Client->reqMktData( 1006, ContractSamples::USStock(), "mdoff,292:BT", false,
                            false, TagValueListSPtr() );
    p_Client->reqMktData( 1007, ContractSamples::USStock(), "mdoff,292:FLY", false,
                            false, TagValueListSPtr() );
    p_Client->reqMktData( 1008, ContractSamples::USStock(), "mdoff,292:MT", false,
                            false, TagValueListSPtr() );
    p_Client->reqMktData( 1009, ContractSamples::BTbroadtapeNewsFeed(),
                            "mdoff,292", false, false, TagValueListSPtr() );
    p_Client->reqMktData( 1010, ContractSamples::BZbroadtapeNewsFeed(),
                            "mdoff,292", false, false, TagValueListSPtr() );
    p_Client->reqMktData( 1011, ContractSamples::FLYbroadtapeNewsFeed(),
                            "mdoff,292", false, false, TagValueListSPtr() );
    p_Client->reqMktData( 1012, ContractSamples::MTbroadtapeNewsFeed(),
                            "mdoff,292", false, false, TagValueListSPtr() );
    p_Client->reqMktData( 1013, ContractSamples::USOptionContract(), "", false,
                            false, TagValueListSPtr() );
    p_Client->reqMktData( 1014, ContractSamples::SimpleFuture(), "mdoff,588",
                            false, false, TagValueListSPtr() );
    p_Client->reqMktData( 1015, ContractSamples::SimpleFuture(), "", false, false,
                            TagValueListSPtr() );
    p_Client->reqMktData( 1016, ContractSamples::USStockAtSmart(), "mdoff,105",
                            false, false, TagValueListSPtr() );
                            */
    /*
// Each regulatory snapshot incurs a fee of 0.01 USD
p_Client->reqMktData(1017, ContractSamples::USStock(), "", false, true,
TagValueListSPtr());

    std::this_thread::sleep_for( std::chrono::seconds( 1 ) );

    p_Client->cancelMktData( 1001 );
    p_Client->cancelMktData( 1002 );
    p_Client->cancelMktData( 1003 );
    p_Client->cancelMktData( 1014 );
    p_Client->cancelMktData( 1015 );
    p_Client->cancelMktData( 1016 );

    *p_State = TICKDATAOPERATION_ACK;
    */
}

void ClientBrain::reqHistoricalTicks()
{
    /*
    p_Client->reqHistoricalTicks( 19001, ContractSamples::IBMUSStockAtSmart(),
                                    "20170621 09:38:33", "", 10, "BID_ASK", 1, true,
                                    TagValueListSPtr() );
    p_Client->reqHistoricalTicks( 19002, ContractSamples::IBMUSStockAtSmart(),
                                    "20170621 09:38:33", "", 10, "MIDPOINT", 1, true,
                                    TagValueListSPtr() );
    p_Client->reqHistoricalTicks( 19003, ContractSamples::IBMUSStockAtSmart(),
                                    "20170621 09:38:33", "", 10, "TRADES", 1, true,
                                    TagValueListSPtr() );
    *p_State = REQHISTORICALTICKS_ACK;
    */
}

void ClientBrain::reqTickByTickData()
{
    /*
    p_Client->reqTickByTickData( 20001, ContractSamples::EuropeanStock(), "Last",
                                    0, false );
    p_Client->reqTickByTickData( 20002, ContractSamples::EuropeanStock(),
                                    "AllLast", 0, false );
    p_Client->reqTickByTickData( 20003, ContractSamples::EuropeanStock(), "BidAsk",
                                    0, true );
    p_Client->reqTickByTickData( 20004, ContractSamples::EurGbpFx(), "MidPoint", 0,
                                    false );

    std::this_thread::sleep_for( std::chrono::seconds( 10 ) );

    p_Client->cancelTickByTickData( 20001 );
    p_Client->cancelTickByTickData( 20002 );
    p_Client->cancelTickByTickData( 20003 );
    p_Client->cancelTickByTickData( 20004 );

    p_Client->reqTickByTickData( 20005, ContractSamples::EuropeanStock(), "Last",
                                    10, false );
    p_Client->reqTickByTickData( 20006, ContractSamples::EuropeanStock(),
                                    "AllLast", 10, false );
    p_Client->reqTickByTickData( 20007, ContractSamples::EuropeanStock(), "BidAsk",
                                    10, false );
    p_Client->reqTickByTickData( 20008, ContractSamples::EurGbpFx(), "MidPoint",
                                    10, true );

    std::this_thread::sleep_for( std::chrono::seconds( 10 ) );

    p_Client->cancelTickByTickData( 20005 );
    p_Client->cancelTickByTickData( 20006 );
    p_Client->cancelTickByTickData( 20007 );
    p_Client->cancelTickByTickData( 20008 );

    *p_State = REQTICKBYTICKDATA_ACK;
    */
}

void ClientBrain::marketDepthOperations()
{
    /*
    p_Client->reqMktDepth( 2001, ContractSamples::EurGbpFx(), 5, false,
                            TagValueListSPtr() );
    std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
    p_Client->cancelMktDepth( 2001, false );

    p_Client->reqMktDepth( 2002, ContractSamples::EuropeanStock(), 5, true,
                            TagValueListSPtr() );
    std::this_thread::sleep_for( std::chrono::seconds( 5 ) );
    p_Client->cancelMktDepth( 2002, true );

    *p_State = MARKETDEPTHOPERATION_ACK;
    */
}

void ClientBrain::realTimeBars()
{
    /*
    p_Client->reqRealTimeBars( 3001, ContractSamples::EurGbpFx(), 5, "MIDPOINT",
                                true, TagValueListSPtr() );
    std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
    p_Client->cancelRealTimeBars( 3001 );

    *p_State = REALTIMEBARS_ACK;
    */
}

void ClientBrain::realtimeBar( TickerId reqId, long time, double open, double high,
                               double low, double close, long volume, double wap,
                               int count )
{
    printf( "RealTimeBars. %ld - Time: %ld, Open: %g, High: %g, Low: %g, Close: "
            "%g, Volume: %ld, Count: %d, WAP: %g\n",
            reqId, time, open, high, low, close, volume, count, wap );
}

void ClientBrain::historicalDataRequests()
{
    /*
    std::time_t     rawtime;
    std::tm*        timeinfo;
    array<char, 80> queryTime;

    std::time( &rawtime );
    timeinfo = std::localtime( &rawtime );
    std::strftime( queryTime.data(), 80, "%Y%m%d %H:%M:%S", timeinfo );

    p_Client->reqHistoricalData( 4001, ContractSamples::EurGbpFx(),
                                    queryTime.data(), "1 M", "1 day", "MIDPOINT", 1,
                                    1, false, TagValueListSPtr() );
    p_Client->reqHistoricalData( 4002, ContractSamples::EuropeanStock(),
                                    queryTime.data(), "10 D", "1 min", "TRADES", 1, 1,
                                    false, TagValueListSPtr() );
    std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
    p_Client->cancelHistoricalData( 4001 );
    p_Client->cancelHistoricalData( 4002 );

    *p_State = HISTORICALDATAREQUESTS_ACK;
    */
}

void ClientBrain::reqMatchingSymbols()
{
    p_Client->reqMatchingSymbols( 11001, "IBM" );
    *p_State = State::SYMBOLSAMPLES_ACK;
}

void ClientBrain::reqMktDepthExchanges()
{
    p_Client->reqMktDepthExchanges();

    *p_State = State::REQMKTDEPTHEXCHANGES_ACK;
}

void ClientBrain::marketScanners()
{
    /*
    p_Client->reqScannerParameters();
    std::this_thread::sleep_for( std::chrono::seconds( 2 ) );

    p_Client->reqScannerSubscription(
        7001, ScannerSubscriptionSamples::HotUSStkByVolume(), TagValueListSPtr(),
        TagValueListSPtr() );

    TagValueSPtr t1( new TagValue( "usdMarketCapAbove", "10000" ) );
    TagValueSPtr t2( new TagValue( "optVolumeAbove", "1000" ) );
    TagValueSPtr t3( new TagValue( "usdMarketCapAbove", "100000000" ) );

    TagValueListSPtr TagValues( new TagValueList() );
    TagValues->push_back( t1 );
    TagValues->push_back( t2 );
    TagValues->push_back( t3 );

    p_Client->reqScannerSubscription(
        7002, ScannerSubscriptionSamples::HotUSStkByVolume(), TagValueListSPtr(),
        TagValues ); // requires TWS v973+

    TagValueSPtr     t( new TagValue( "underConID", "265598" ) );
    TagValueListSPtr AAPLConIDTag( new TagValueList() );
    AAPLConIDTag->push_back( t );
    p_Client->reqScannerSubscription(
        7003, ScannerSubscriptionSamples::ComplexOrdersAndTrades(),
        TagValueListSPtr(), AAPLConIDTag ); // requires TWS v975+

    std::this_thread::sleep_for( std::chrono::seconds( 2 ) );

    p_Client->cancelScannerSubscription( 7001 );
    p_Client->cancelScannerSubscription( 7002 );

    *p_State = MARKETSCANNERS_ACK;
    */
}

void ClientBrain::scannerParameters( const std::string& xml )
{
    printf( "ScannerParameters. %s\n", xml.c_str() );
}

void ClientBrain::scannerData( int reqId, int rank,
                               const ContractDetails& contractDetails,
                               const std::string&     distance,
                               const std::string&     benchmark,
                               const std::string&     projection,
                               const std::string&     legsStr )
{
    printf( "ScannerData. %d - Rank: %d, Symbol: %s, SecType: %s, Currency: %s, "
            "Distance: %s, Benchmark: %s, Projection: %s, Legs String: %s\n",
            reqId, rank, contractDetails.contract.symbol.c_str(),
            contractDetails.contract.secType.c_str(),
            contractDetails.contract.currency.c_str(), distance.c_str(),
            benchmark.c_str(), projection.c_str(), legsStr.c_str() );
}

void ClientBrain::scannerDataEnd( int reqId )
{
    printf( "ScannerDataEnd. %d\n", reqId );
}

void ClientBrain::reqHistogramData()
{
    /*
    p_Client->reqHistogramData( 15001, ContractSamples::IBMUSStockAtSmart(), false,
                                "1 weeks" );
    std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
    p_Client->cancelHistogramData( 15001 );
    *p_State = REQHISTOGRAMDATA_ACK;
    */
}

void ClientBrain::histogramData( int reqId, const HistogramDataVector& data )
{
    printf( "Histogram. ReqId: %d, data length: %lu\n", reqId, data.size() );

    for( auto item : data )
    {
        printf( "\t price: %f, size: %lld\n", item.price, item.size );
    }
}

void ClientBrain::tickOptionComputation( TickerId tickerId, TickType tickType,
                                         double impliedVol, double delta,
                                         double optPrice, double pvDividend,
                                         double gamma, double vega, double theta,
                                         double undPrice ) {}

void ClientBrain::tickEFP( TickerId tickerId, TickType tickType, double basisPoints,
                           const std::string& formattedBasisPoints,
                           double totalDividends, int holdDays,
                           const std::string& futureLastTradeDate,
                           double dividendImpact, double dividendsToLastTradeDate ) {}

void ClientBrain::tickByTickBidAsk( int reqId, time_t time, double bidPrice,
                                    double askPrice, int bidSize, int askSize,
                                    const TickAttribBidAsk& tickAttribBidAsk )
{
    printf( "Tick-By-Tick. ReqId: %d, TickType: BidAsk, Time: %s, BidPrice: %g, "
            "AskPrice: %g, BidSize: %d, AskSize: %d, BidPastLow: %d, AskPastHigh: "
            "%d\n",
            reqId, ctime( &time ), bidPrice, askPrice, bidSize, askSize,
            (int)tickAttribBidAsk.bidPastLow, (int)tickAttribBidAsk.askPastHigh );
}

void ClientBrain::tickByTickMidPoint( int reqId, time_t time, double midPoint )
{
    printf(
        "Tick-By-Tick. ReqId: %d, TickType: MidPoint, Time: %s, MidPoint: %g\n",
        reqId, ctime( &time ), midPoint );
}

void ClientBrain::securityDefinitionOptionalParameter(
    int reqId, const std::string& exchange, int underlyingConId,
    const std::string& tradingClass, const std::string& multiplier,
    const std::set<std::string>& expirations, const std::set<double>& strikes )
{
    printf( "Security Definition Optional Parameter. Request: %d, Trading Class: "
            "%s, Multiplier: %s\n",
            reqId, tradingClass.c_str(), multiplier.c_str() );
}

void ClientBrain::securityDefinitionOptionalParameterEnd( int reqId )
{
    printf( "Security Definition Optional Parameter End. Request: %d\n", reqId );
}

/** Functions helpful to TBBroker
 * Placing orders
 * OCA orders
 * Order status
 * Commission report
 */
void ClientBrain::orderOperations()
{
    /*
    // The parameter is always ignored.
    p_Client->reqIds( -1 );
    p_Client->reqAllOpenOrders();
    p_Client->reqAutoOpenOrders( true );
    p_Client->reqOpenOrders();

    // remember to ALWAYS increment the nextValidId after placing an order so it
    // can be used for the next one!
    //p_Client->placeOrder( ( *p_OrderId )++, ContractSamples::USStock(),OrderSamples::LimitOrder( "SELL", 1, 50 ) );

    // p_Client->placeOrder((*p_OrderId)++, ContractSamples::OptionAtBox(),
    // OrderSamples::Block("BUY", 50, 20)); p_Client->placeOrder((*p_OrderId)++,
    // ContractSamples::OptionAtBox(), OrderSamples::BoxTop("SELL", 10));
    // p_Client->placeOrder((*p_OrderId)++,
    // ContractSamples::FutureComboContract(),
    // OrderSamples::ComboLimitOrder("SELL", 1, 1, false));
    // p_Client->placeOrder((*p_OrderId)++, ContractSamples::StockComboContract(),
    // OrderSamples::ComboMarketOrder("BUY", 1, false));
    // p_Client->placeOrder((*p_OrderId)++,
    // ContractSamples::OptionComboContract(),
    // OrderSamples::ComboMarketOrder("BUY", 1, true));
    // p_Client->placeOrder((*p_OrderId)++, ContractSamples::StockComboContract(),
    // OrderSamples::LimitOrderForComboWithLegPrices("BUY", 1,
    // std::vector<double>(10, 5), true)); p_Client->placeOrder((*p_OrderId)++,
    // ContractSamples::USStock(), OrderSamples::Discretionary("SELL", 1, 45,
    // 0.5)); p_Client->placeOrder((*p_OrderId)++, ContractSamples::OptionAtBox(),
    // OrderSamples::LimitIfTouched("BUY", 1, 30, 34));
    // p_Client->placeOrder((*p_OrderId)++, ContractSamples::USStock(),
    // OrderSamples::LimitOnClose("SELL", 1, 34));
    // p_Client->placeOrder((*p_OrderId)++, ContractSamples::USStock(),
    // OrderSamples::LimitOnOpen("BUY", 1, 35));
    // p_Client->placeOrder((*p_OrderId)++, ContractSamples::USStock(),
    // OrderSamples::MarketIfTouched("BUY", 1, 35));
    // p_Client->placeOrder((*p_OrderId)++, ContractSamples::USStock(),
    // OrderSamples::MarketOnClose("SELL", 1));
    // p_Client->placeOrder((*p_OrderId)++, ContractSamples::USStock(),
    // OrderSamples::MarketOnOpen("BUY", 1)); p_Client->placeOrder((*p_OrderId)++,
    // ContractSamples::USStock(), OrderSamples::MarketOrder("SELL", 1));
    // p_Client->placeOrder((*p_OrderId)++, ContractSamples::USStock(),
    // OrderSamples::MarketToLimit("BUY", 1)); p_Client->placeOrder((*p_OrderId)++,
    // ContractSamples::OptionAtIse(), OrderSamples::MidpointMatch("BUY", 1));
    // p_Client->placeOrder((*p_OrderId)++, ContractSamples::USStock(),
    // OrderSamples::Stop("SELL", 1, 34.4)); p_Client->placeOrder((*p_OrderId)++,
    // ContractSamples::USStock(), OrderSamples::StopLimit("BUY", 1, 35, 33));
    // p_Client->placeOrder((*p_OrderId)++, ContractSamples::USStock(),
    // OrderSamples::StopWithProtection("SELL", 1, 45));
    // p_Client->placeOrder((*p_OrderId)++, ContractSamples::USStock(),
    // OrderSamples::SweepToFill("BUY", 1, 35));
    // p_Client->placeOrder((*p_OrderId)++, ContractSamples::USStock(),
    // OrderSamples::TrailingStop("SELL", 1, 0.5, 30));
    // p_Client->placeOrder((*p_OrderId)++, ContractSamples::USStock(),
    // OrderSamples::TrailingStopLimit("BUY", 1, 2, 5, 50));
    //p_Client->placeOrder( ( *p_OrderId )++, ContractSamples::USStockAtSmart(),OrderSamples::Midprice( "BUY", 1, 150 ) );
    //p_Client->placeOrder( ( *p_OrderId )++, ContractSamples::USStockAtSmart(),OrderSamples::LimitOrderWithCashQty( "BUY", 1, 30, 5000 ) );

    std::this_thread::sleep_for( std::chrono::seconds( 1 ) );

    p_Client->cancelOrder( *p_OrderId - 1 );
    p_Client->reqGlobalCancel();
    p_Client->reqExecutions( 10001, ExecutionFilter() );
    p_Client->reqCompletedOrders( false );

    *p_State = ORDEROPERATIONS_ACK;*/
}

void ClientBrain::ocaSamples()
{
    /*
    std::vector<Order> ocaOrders;
    ocaOrders.push_back( OrderSamples::LimitOrder( "BUY", 1, 10 ) );
    ocaOrders.push_back( OrderSamples::LimitOrder( "BUY", 1, 11 ) );
    ocaOrders.push_back( OrderSamples::LimitOrder( "BUY", 1, 12 ) );
    for( auto& index : ocaOrders )
    {
        OrderSamples::OneCancelsAll( "TestOca", index, 2 );
        p_Client->placeOrder( ( *p_OrderId )++, ContractSamples::USStock(), index );
    }

    *p_State = OCASAMPLES_ACK;
    */
}

void ClientBrain::commissionReport( const CommissionReport& commissionReport )
{
    //printf( "CommissionReport. %s - %g %s RPNL %g\n", commissionReport.execId.c_str(), commissionReport.commission, commissionReport.currency.c_str(), commissionReport.realizedPNL );
}

/** What are these?
 *
 */
void ClientBrain::bracketSample()
{
    // TODO: read about what this is
    /*
    Order parent;
    Order takeProfit;
    Order stopLoss;
    OrderSamples::BracketOrder( ( *p_OrderId )++, parent, takeProfit, stopLoss,
                                "BUY", 100, 30, 40, 20 );
    p_Client->placeOrder( parent.orderId, ContractSamples::EuropeanStock(),
                            parent );
    p_Client->placeOrder( takeProfit.orderId, ContractSamples::EuropeanStock(),
                            takeProfit );
    p_Client->placeOrder( stopLoss.orderId, ContractSamples::EuropeanStock(),
                            stopLoss );

    *p_State = BRACKETSAMPLES_ACK;
    */
}

void ClientBrain::hedgeSample()
{
    // F Hedge order
    // Parent order on a contract which currency differs from your base currency
    /*Order parent = OrderSamples::LimitOrder( "BUY", 100, 10 );
    parent.orderId = ( *p_OrderId )++;
    parent.transmit = false;
    // Hedge on the currency conversion
    Order hedge = OrderSamples::MarketFHedge( parent.orderId, "BUY" );
    // Place the parent first...
    p_Client->placeOrder( parent.orderId, ContractSamples::EuropeanStock(),
                            parent );
    // Then the hedge order
    p_Client->placeOrder( ( *p_OrderId )++, ContractSamples::EurGbpFx(), hedge );

    *p_State = HEDGESAMPLES_ACK;*/
}

void ClientBrain::rerouteCFDOperations()
{
    /*
    p_Client->reqMktData( 16001, ContractSamples::USStockCFD(), "", false, false,
                            TagValueListSPtr() );
    std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
    p_Client->reqMktData( 16002, ContractSamples::EuropeanStockCFD(), "", false,
                            false, TagValueListSPtr() );
    std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
    p_Client->reqMktData( 16003, ContractSamples::CashCFD(), "", false, false,
                            TagValueListSPtr() );
    std::this_thread::sleep_for( std::chrono::seconds( 1 ) );

    p_Client->reqMktDepth( 16004, ContractSamples::USStockCFD(), 10, false,
                            TagValueListSPtr() );
    std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
    p_Client->reqMktDepth( 16005, ContractSamples::EuropeanStockCFD(), 10, false,
                            TagValueListSPtr() );
    std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
    p_Client->reqMktDepth( 16006, ContractSamples::CashCFD(), 10, false,
                            TagValueListSPtr() );
    std::this_thread::sleep_for( std::chrono::seconds( 1 ) );

    *p_State = REROUTECFD_ACK;
    */
}

void ClientBrain::marketRuleOperations()
{
    /*
    p_Client->reqContractDetails( 17001, ContractSamples::IBMBond() );
    p_Client->reqContractDetails( 17002, ContractSamples::IBKRStk() );

    std::this_thread::sleep_for( std::chrono::seconds( 2 ) );

    p_Client->reqMarketRule( 26 );
    p_Client->reqMarketRule( 635 );
    p_Client->reqMarketRule( 1388 );

    *p_State = MARKETRULE_ACK;
    */
}

void ClientBrain::continuousFuturesOperations()
{
    /*
    p_Client->reqContractDetails( 18001, ContractSamples::ContFut() );

    std::time_t     rawtime;
    std::tm*        timeinfo;
    array<char, 80> queryTime;

    std::time( &rawtime );
    timeinfo = std::localtime( &rawtime );
    std::strftime( queryTime.data(), 80, "%Y%m%d %H:%M:%S", timeinfo );

    p_Client->reqHistoricalData( 18002, ContractSamples::ContFut(),
                                    queryTime.data(), "1 Y", "1 month", "TRADES", 0,
                                    1, false, TagValueListSPtr() );

    std::this_thread::sleep_for( std::chrono::seconds( 10 ) );

    p_Client->cancelHistoricalData( 18002 );

    *p_State = CONTFUT_ACK;
    */
}

void ClientBrain::whatIfSamples()
{
    /*
    p_Client->placeOrder( ( *p_OrderId )++, ContractSamples::USStockAtSmart(),
                            OrderSamples::WhatIfLimitOrder( "BUY", 200, 120 ) );

    *p_State = WHATIFSAMPLES_ACK;
    */
}

/** What are these?
 *
 */
void ClientBrain::deltaNeutralValidation(
    int reqId, const DeltaNeutralContract& deltaNeutralContract )
{
    printf( "DeltaNeutralValidation. %d, ConId: %ld, Delta: %g, Price: %g\n",
            reqId, deltaNeutralContract.conId, deltaNeutralContract.delta,
            deltaNeutralContract.price );
}

void ClientBrain::verifyMessageAPI( const std::string& apiData )
{
    printf( "verifyMessageAPI: %s\b", apiData.c_str() );
}

void ClientBrain::verifyCompleted( bool isSuccessful, const std::string& errorText )
{
    printf( "verifyCompleted. IsSuccessfule: %d - Error: %s\n", (int)isSuccessful,
            errorText.c_str() );
}

void ClientBrain::verifyAndAuthMessageAPI( const std::string& apiDatai,
                                           const std::string& xyzChallenge )
{
    printf( "verifyAndAuthMessageAPI: %s %s\n", apiDatai.c_str(),
            xyzChallenge.c_str() );
}

void ClientBrain::verifyAndAuthCompleted( bool               isSuccessful,
                                          const std::string& errorText )
{
    printf( "verifyAndAuthCompleted. IsSuccessful: %d - Error: %s\n",
            (int)isSuccessful, errorText.c_str() );
    if( isSuccessful )
    {
        p_Client->startApi();
    }
}

void ClientBrain::displayGroupList( int reqId, const std::string& groups )
{
    printf( "Display Group List. ReqId: %d, Groups: %s\n", reqId, groups.c_str() );
}

void ClientBrain::displayGroupUpdated( int reqId, const std::string& contractInfo )
{
    std::cout << "Display Group Updated. ReqId: " << reqId
              << ", Contract Info: " << contractInfo << std::endl;
}

void ClientBrain::softDollarTiers( int                                reqId,
                                   const std::vector<SoftDollarTier>& tiers )
{
    printf( "Soft dollar tiers (%lu):", tiers.size() );

    for( const auto& index : tiers )
    {
        printf( "%s\n", index.displayName().c_str() );
    }
}

void ClientBrain::familyCodes( const std::vector<FamilyCode>& familyCodes )
{
    printf( "Family codes (%lu):\n", familyCodes.size() );

    for( unsigned int i = 0; i < familyCodes.size(); i++ )
    {
        printf( "Family code [%d] - accountID: %s familyCodeStr: %s\n", i,
                familyCodes[i].accountID.c_str(),
                familyCodes[i].familyCodeStr.c_str() );
    }
}

void ClientBrain::symbolSamples(
    int reqId, const std::vector<ContractDescription>& contractDescriptions )
{
    printf( "Symbol Samples (total=%lu) reqId: %d\n", contractDescriptions.size(),
            reqId );

    for( unsigned int i = 0; i < contractDescriptions.size(); i++ )
    {
        Contract                 contract = contractDescriptions[i].contract;
        std::vector<std::string> derivativeSecTypes =
            contractDescriptions[i].derivativeSecTypes;
        printf( "Contract (%u): %ld %s %s %s %s, ", i, contract.conId,
                contract.symbol.c_str(), contract.secType.c_str(),
                contract.primaryExchange.c_str(), contract.currency.c_str() );
        printf( "Derivative Sec-types (%lu):", derivativeSecTypes.size() );
        for( auto& index : derivativeSecTypes )
        {
            printf( " %s", index.c_str() );
        }
        printf( "\n" );
    }
}

void ClientBrain::mktDepthExchanges(
    const std::vector<DepthMktDataDescription>& depthMktDataDescriptions )
{
    printf( "Mkt Depth Exchanges (%lu):\n", depthMktDataDescriptions.size() );

    for( unsigned int i = 0; i < depthMktDataDescriptions.size(); i++ )
    {
        printf( "Depth Mkt Data Description [%d] - exchange: %s secType: %s "
                "listingExch: %s serviceDataType: %s aggGroup: %s\n",
                i, depthMktDataDescriptions[i].exchange.c_str(),
                depthMktDataDescriptions[i].secType.c_str(),
                depthMktDataDescriptions[i].listingExch.c_str(),
                depthMktDataDescriptions[i].serviceDataType.c_str(),
                depthMktDataDescriptions[i].aggGroup != INT_MAX
                    ? std::to_string( depthMktDataDescriptions[i].aggGroup ).c_str()
                    : "" );
    }
}

void ClientBrain::smartComponents( int reqId, const SmartComponentsMap& theMap )
{
    printf( "Smart components: (%lu):\n", theMap.size() );

    for( const auto& i : theMap )
    {
        printf( " bit number: %d exchange: %s exchange letter: %c\n", i.first,
                std::get<0>( i.second ).c_str(), std::get<1>( i.second ) );
    }
}

void ClientBrain::rerouteMktDataReq( int reqId, int conid,
                                     const std::string& exchange )
{
    printf( "Re-route market data request. ReqId: %d, ConId: %d, Exchange: %s\n",
            reqId, conid, exchange.c_str() );
}

void ClientBrain::rerouteMktDepthReq( int reqId, int conid,
                                      const std::string& exchange )
{
    printf( "Re-route market depth request. ReqId: %d, ConId: %d, Exchange: %s\n",
            reqId, conid, exchange.c_str() );
}

void ClientBrain::marketRule( int                                marketRuleId,
                              const std::vector<PriceIncrement>& priceIncrements )
{
    printf( "Market Rule Id: %d\n", marketRuleId );
    for( const auto& index : priceIncrements )
    {
        printf( "Low Edge: %g, Increment: %g\n", index.lowEdge, index.increment );
    }
}

void ClientBrain::orderBound( long long orderId, int apiClientId, int apiOrderId )
{
    printf( "Order bound. OrderId: %lld, ApiClientId: %d, ApiOrderId: %d\n",
            orderId, apiClientId, apiOrderId );
}

void ClientBrain::receiveFA( faDataType pFaDataType, const std::string& cxml )
{
    std::cout << "Receiving FA: " << (int)pFaDataType << std::endl
              << cxml << std::endl;
}

void ClientBrain::accountUpdateMultiEnd( int reqId )
{
    printf( "Account Update Multi End. Request: %d\n", reqId );
}

void ClientBrain::contractDetailsEnd( int reqId )
{
    printf( "ContractDetailsEnd. %d\n", reqId );
}

void ClientBrain::tickNews( int tickerId, time_t timeStamp,
                            const std::string& providerCode,
                            const std::string& articleId, const std::string& headline,
                            const std::string& extraData )
{
    // printf( "News Tick. TickerId: %d, TimeStamp: %s, ProviderCode: %s,
    // ArticleId: %s, Headline: %s, ExtraData: %s\n", tickerId, ctime( &( timeStamp
    // /= 1000 ) ), providerCode.c_str(), articleId.c_str(), headline.c_str(),
    // extraData.c_str() );
}

void ClientBrain::accountSummaryEnd( int reqId )
{
    printf( "AccountSummaryEnd. Req Id: %d\n", reqId );
}

void ClientBrain::fundamentalData( TickerId reqId, const std::string& data )
{
    printf( "FundamentalData. ReqId: %ld, %s\n", reqId, data.c_str() );
}

void ClientBrain::updateNewsBulletin( int msgId, int msgType,
                                      const std::string& newsMessage,
                                      const std::string& originExch )
{
    printf( "News Bulletins. %d - Type: %d, Message: %s, Exchange of Origin: %s\n",
            msgId, msgType, newsMessage.c_str(), originExch.c_str() );
}

void ClientBrain::contractDetails( int                    reqId,
                                   const ContractDetails& contractDetails )
{
    printf( "ContractDetails begin. ReqId: %d\n", reqId );
    // printContractMsg( contractDetails.contract );
    // printContractDetailsMsg( contractDetails );
    printf( "ContractDetails end. ReqId: %d\n", reqId );
}

void ClientBrain::tickByTickAllLast( int reqId, int tickType, time_t time,
                                     double price, int size,
                                     const TickAttribLast& tickAttribLast,
                                     const std::string&    exchange,
                                     const std::string&    specialConditions )
{
    printf(
        "Tick-By-Tick. ReqId: %d, TickType: %s, Time: %s, Price: %g, Size: %d, "
        "PastLimit: %d, Unreported: %d, Exchange: %s, SpecialConditions:%s\n",
        reqId, ( tickType == 1 ? "Last" : "AllLast" ), ctime( &time ), price, size,
        (int)tickAttribLast.pastLimit, (int)tickAttribLast.unreported,
        exchange.c_str(), specialConditions.c_str() );
}

void ClientBrain::historicalNewsEnd( int requestId, bool hasMore )
{
    printf( "Historical News End. RequestId: %d, HasMore: %s\n", requestId,
            ( hasMore ? "true" : " false" ) );
}

void ClientBrain::newsArticle( int requestId, int articleType,
                               const std::string& articleText )
{
    printf( "News Article. Request Id: %d, Article Type: %d\n", requestId,
            articleType );
    if( articleType == 0 )
    {
        printf( "News Article Text (text or html): %s\n", articleText.c_str() );
    }
    else if( articleType == 1 )
    {
        std::string path;
#if defined( IB_WIN32 )
        TCHAR s[200];
        GetCurrentDirectory( 200, s );
        path = s + std::string( "\\MST$06f53098.pdf" );
#elif defined( IB_POSIX )
        array<char, 1024> s;
        if( getcwd( s.data(), s.size() ) == nullptr )
        {
            printf( "getcwd() error\n" );
            return;
        }
        path = s.data() + std::string( "/MST$06f53098.pdf" );
#endif
        /*std::vector<std::uint8_t> bytes = Utils::base64_decode( articleText );
        std::ofstream             outfile( path, std::ios::out | std::ios::binary );
        outfile.write( (const char*)bytes.data(), bytes.size() );
        printf( "Binary/pdf article was saved to: %s\n", path.c_str() );
        */
    }
}

void ClientBrain::historicalNews( int requestId, const std::string& time,
                                  const std::string& providerCode,
                                  const std::string& articleId,
                                  const std::string& headline )
{
    printf( "Historical News. RequestId: %d, Time: %s, ProviderCode: %s, "
            "ArticleId: %s, Headline: %s\n",
            requestId, time.c_str(), providerCode.c_str(), articleId.c_str(),
            headline.c_str() );
}

void ClientBrain::accountUpdateMulti( int reqId, const std::string& account,
                                      const std::string& modelCode,
                                      const std::string& key,
                                      const std::string& value,
                                      const std::string& currency )
{
    printf( "AccountUpdate Multi. Request: %d, Account: %s, ModelCode: %s, Key, "
            "%s, Value: %s, Currency: %s\n",
            reqId, account.c_str(), modelCode.c_str(), key.c_str(), value.c_str(),
            currency.c_str() );
}

void ClientBrain::replaceFAEnd( int reqId, const string& arg )
{
}