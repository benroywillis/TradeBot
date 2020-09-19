#include "ClientBrain.h"
#include "AccountSummaryTags.h"
#include "ClientAccount.h"
#include "ClientBroker.h"
#include "ClientData.h"
#include "Contract.h"
#include "ContractSamples.h"
#include "EClientSocket.h"
#include "Execution.h"
#include "Order.h"
#include "OrderState.h"
#include "Strategy.h"
#include <chrono>
#include <iostream>
#include <spdlog/spdlog.h>
#include <thread>

using namespace std;
using namespace ClientSpace;

ClientBrain::ClientBrain()
    : Account( make_shared<ClientAccount>( 0, p_Client, p_State ) ),
      Data( make_shared<ClientData>( p_Client, p_State ) ),
      Broker( make_shared<ClientBroker>( p_Client, p_State, p_OrderId ) )
{
    Strategy = make_shared<BTStrategy>();
    reqId = 10000;
}

ClientBrain::ClientBrain( shared_ptr<BTStrategy> newStrategy )
    : Account( make_shared<ClientAccount>( 0, p_Client, p_State ) ),
      Data( make_shared<ClientData>( p_Client, p_State ) ),
      Broker( make_shared<ClientBroker>( p_Client, p_State, p_OrderId ) )
{
    Strategy = move( newStrategy );
    reqId = 10000;
}

ClientBrain::ClientBrain( shared_ptr<ClientData>        newData,
                          const shared_ptr<BTStrategy>& newStrategy )
    : Account( make_shared<ClientAccount>( 0, p_Client, p_State ) ),
      Broker( make_shared<ClientBroker>( p_Client, p_State, p_OrderId ) )
{
    Strategy = newStrategy;
    reqId = 10000;
    Data = move( newData );
    Data->addClient( p_Client );
    Data->addState( p_State );
}

ClientBrain::ClientBrain( const shared_ptr<ClientAccount>& newAccount,
                          const shared_ptr<ClientData>&    newData,
                          const shared_ptr<BTStrategy>&    newStrategy )
    : Broker( make_shared<ClientBroker>( p_Client, p_State, p_OrderId ) )
{
    Account = newAccount;
    Account->addClient( p_Client );
    Account->addState( p_State );
    Data = newData;
    Data->addClient( p_Client );
    Data->addState( p_State );
    Strategy = newStrategy;
}

long ClientBrain::getNextReqId() { return reqId++; }

void ClientBrain::setConnectOptions( const std::string& connectOptions )
{
    p_Client->setConnectOptions( connectOptions );
}

void ClientBrain::init()
{
    Account->init();
    Data->init();
}

bool ClientBrain::connect( const char* host, int port, int clientId )
{
    clientID = clientId;
    *p_State = CONNECT;
    string hostName = !( ( host != nullptr ) && ( *host ) != 0 ) ? "127.0.0.1" : host;
    spdlog::info( "Connecting to " + hostName + ": " + to_string( port ) +
                  " clientID: " + to_string( clientId ) );
    bool bRes = p_Client->eConnect( host, port, clientId, *p_ExtraAuth );
    if( bRes )
    {
        spdlog::info( "Connected to " + p_Client->host() + ": " +
                      to_string( p_Client->port() ) +
                      " clientID: " + to_string( clientId ) );
        p_Reader = make_shared<EReader>( p_Client.get(), &m_osSignal );
        p_Reader->start();
        *p_State = CONNECTSUCCESS;
    }
    else
    {
        *p_State = CONNECTFAIL;
        spdlog::error( "Cannot connect to " + p_Client->host() + ": " +
                       to_string( p_Client->port() ) +
                       " clientID: " + to_string( clientId ) );
    }
    // if this isn't here the client will immediately disconnect
    this_thread::sleep_for( chrono::milliseconds( 10 ) );
    return bRes;
}

void ClientBrain::disconnect() const
{
    p_Client->eDisconnect();
    *p_State = DISCONNECTED;
    spdlog::info( "Disconnected" );
}

bool ClientBrain::isConnected() const { return p_Client->isConnected(); }

void ClientBrain::connectionClosed()
{
    spdlog::info( "Connection Closed" );
    *p_State = DISCONNECTED;
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
    p_Client->reqHeadTimestamp( 14001, ContractSamples::EurGbpFx(), "MIDPOINT", 1,
                                1 );
    std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
    p_Client->cancelHeadTimestamp( 14001 );

    *p_State = REQHEADTIMESTAMP_ACK;
}

void ClientBrain::headTimestamp( int reqId, const std::string& headTimestamp )
{
    spdlog::info( "Head time stamp. ReqId: " + to_string( reqId ) +
                  " - Head time stamp: " + headTimestamp );
}

void ClientBrain::reqCurrentTime()
{
    // set ping deadline to "now + n seconds"
    m_sleepDeadline = time( nullptr ) + PING_DEADLINE;
    *p_State = PING_ACK;
    p_Client->reqCurrentTime();
}

void ClientBrain::currentTime( long time )
{
    if( *p_State == PING_ACK )
    {
        auto       t = (time_t)time;
        struct tm* timeinfo = localtime( &t );
        spdlog::info( "The current date/time is " + string( asctime( timeinfo ) ) );
        auto now = ::time( nullptr );
        m_sleepDeadline = now + SLEEP_BETWEEN_PINGS;
        *p_State = IDLE;
    }
}

void ClientBrain::nextValidId( OrderId orderId )
{
    spdlog::info( "Next Valid Id: " + to_string( orderId ) );
    *p_OrderId = orderId;
}

void ClientBrain::managedAccounts( const std::string& accountsList )
{
    Account->accountID = accountsList; // this list is always a single ID for now
}

void ClientBrain::error( int id, int errorCode, const std::string& errorString )
{
    spdlog::error( "Error: ID " + to_string( id ) + " Code " + to_string( errorCode ) +
                   " MSG " + errorString );
    if( inter )
    {
        disconnect();
        exit( -1 );
    }
    else if( errorCode == 322 )
    {
        // more than 50 data requests at once, so this vectorId won't be answered
        Data->openHistRequests.erase( (long)id );
    }
    else if( errorCode == 504 )
    {
        // just made a request with p_Client when disconnected
        *p_State = DISCONNECTED;
    }
    else if( errorCode == 201 )
    {
        // just submitted an order in which we didn't have the adequate funds to
        // cover it
        long search = (long)id;
        if( Broker->pendingOrders.find( search ) != Broker->pendingOrders.end() )
        {
            Broker->pendingOrders.erase( Broker->pendingOrders.find( search ) );
        }
        if( Broker->openOrders.find( search ) != Broker->openOrders.end() )
        {
            Broker->openOrders.erase( Broker->openOrders.find( search ) );
        }
    }
}

void ClientBrain::winError( const std::string& str, int lastError )
{
    spdlog::error( "Error: Message " + str + " Code " + to_string( lastError ) );
}

/** Callbacks for account
 *
 */
void ClientBrain::accountSummary( int reqId, const std::string& account,
                                  const std::string& tag,
                                  const std::string& value,
                                  const std::string& currency )
{
    Account->accountID = account;
    Account->cash = stof( value );
    Account->accountCurrency = currency;
    spdlog::info( "Account information now is: ID " + Account->accountID +
                  " tag " + tag + " cash $" + to_string( Account->cash ) +
                  " Currency " + Account->accountCurrency );
}

void ClientBrain::updateAccountValue( const std::string& key,
                                      const std::string& val,
                                      const std::string& currency,
                                      const std::string& accountName )
{
    // spdlog::info( " Account Value Update: Key: " + key + ", Value: " + val + ",
    // Currency: " + currency + ", Account: " + accountName );
    if( key == "CashBalance" )
    {
        Account->cash = atof( val.data() );
    }
    else if( key == "RealizedPnL" )
    {
        Account->PnL = atof( val.data() );
    }
    else if( key == "UnrealizedPnL" )
    {
        Account->UPnL = atof( val.data() );
    }
}

void ClientBrain::updatePortfolio( const Contract& contract, double position,
                                   double marketPrice, double marketValue,
                                   double averageCost, double unrealizedPNL,
                                   double             realizedPNL,
                                   const std::string& accountName )
{
    // if we're initializing the bot
    if( *p_State == INIT )
    {
        // initialize pre-existing positions from the incoming info, including
        // synthetic order and execution objects
        auto order = Order();
        order.account = Account->accountID;
        order.action = position > 0 ? "BUY" : "SELL";
        order.clientId = clientID;
        order.filledQuantity = position;
        auto exec = Execution();
        exec.acctNumber = Account->accountID;
        exec.avgPrice = averageCost;
        exec.clientId = clientID;
        exec.cumQty = position;
        exec.price = averageCost;
        exec.shares = position;
        exec.side = position > 0 ? "BUY" : "SELL";
        exec.time = TimeStamp().toString();
        auto pos = Position( contract, order, exec );
        Account->update( pos );
        spdlog::info( "Updating portfolio: " + contract.symbol +
                      ", size: " + to_string( position ) + ", price per share: $" +
                      to_string( averageCost ) );
    }
}

void ClientBrain::updateAccountTime( const std::string& timeStamp )
{
    spdlog::info( "The last account update was " + timeStamp );
}

void ClientBrain::accountDownloadEnd( const std::string& accountName )
{
    spdlog::info( "Account download ended for " + accountName );
    Account->valid = true;
}

void ClientBrain::position( const std::string& account, const Contract& contract,
                            double position, double avgCost )
{
    // Compare the positions coming in to the positions in the account
    auto search = Account->positions.find( contract.conId );
    if( search != Account->positions.end() )
    {
        const auto* pos = *( search );
        if( pos->getAvgPrice() != avgCost )
        {
            spdlog::error( "Position with symbol " + pos->getContract()->symbol +
                           " secType " + pos->getContract()->secType +
                           " had an inaccuracte average cost!" );
        }
        if( pos->getPositionSize() != position )
        {
            spdlog::error( "Position with symbol " + pos->getContract()->symbol +
                           " secType " + pos->getContract()->secType +
                           " had an inaccuracte position size!" );
        }
    }
}

void ClientBrain::positionEnd() { spdlog::info( "End of positiion update" ); }

/** Callbacks for ClientData
 *
 */
void ClientBrain::tickPrice( TickerId tickerId, TickType field, double price,
                             const TickAttrib& attribs )
{
    auto snapVec = Data->snapMap.find( tickerId );
    auto opVec = Data->optionMap.find( tickerId );
    if( snapVec != Data->snapMap.end() )
    {
        if( field == 1 || field == 2 )
        {
            Data->updatePrice( tickerId, snapVec->second.bidAsk, price, field );
        }
        else if( field == 4 )
        {
            Data->updatePrice( tickerId, snapVec->second.lastTrade, price, field );
        }
    }
    else if( opVec != Data->optionMap.end() )
    {
        if( field == 1 || field == 2 )
        {
            Data->updatePrice( tickerId, opVec->second.bidAsk, price, field );
        }
        else if( field == 4 )
        {
            Data->updatePrice( tickerId, opVec->second.lastTrade, price, field );
        }
    }
}

void ClientBrain::tickSize( TickerId tickerId, TickType field, int size )
{
    auto snapVec = Data->snapMap.find( tickerId );
    auto opVec = Data->optionMap.find( tickerId );
    if( snapVec != Data->snapMap.end() )
    {
        if( field == 0 || field == 3 )
        {
            Data->updateSize( tickerId, snapVec->second.bidAsk, size, field );
        }
        else if( field == 5 )
        {
            Data->updateSize( tickerId, snapVec->second.lastTrade, size, field );
        }
    }
    else if( opVec != Data->optionMap.end() )
    {
        if( field == 0 || field == 3 )
        {
            Data->updateSize( tickerId, opVec->second.bidAsk, size, field );
        }
        else if( field == 5 )
        {
            Data->updateSize( tickerId, opVec->second.lastTrade, size, field );
        }
    }
    else
    {
        spdlog::error( "Could not find an entry in the snapMap or optionMap for "
                       "data request " +
                       to_string( tickerId ) );
    }
}

void ClientBrain::tickOptionComputation( TickerId tickerId, TickType tickType,
                                         double impliedVol, double delta,
                                         double optPrice, double pvDividend,
                                         double gamma, double vega, double theta,
                                         double undPrice )
{
    auto opVec = Data->optionMap.find( tickerId );
    if( opVec != Data->optionMap.end() )
    {
        // undPrice is always crap, don't pass it
        if( tickType == 10 || tickType == 11 )
        {
            Data->updateOptionGreeks( tickerId, opVec->second.bidAsk, impliedVol,
                                      delta, optPrice, pvDividend, gamma, vega, theta,
                                      tickType );
        }
        else if( tickType == 12 )
        {
            Data->updateOptionGreeks( tickerId, opVec->second.lastTrade, impliedVol,
                                      delta, optPrice, pvDividend, gamma, vega, theta,
                                      tickType );
        }
    }
    else
    {
        spdlog::error(
            "Could not find an entry in the option map for data request " +
            to_string( tickerId ) );
    }
}

void ClientBrain::tickGeneric( TickerId tickerId, TickType tickType,
                               double value )
{
    spdlog::info( "New value for " + to_string( tickerId ) + " of type " +
                  to_string( tickType ) + " is " + to_string( value ) );
}

void ClientBrain::tickString( TickerId tickerId, TickType tickType,
                              const std::string& value )
{
    // handles NBBO exchange info for snapshot callbacks
    // spdlog::info( "New value of type " + to_string( tickType ) + " for " +
    // to_string( tickerId ) + " is " + value );
}

void ClientBrain::tickSnapshotEnd( int reqId )
{
    spdlog::info( "Snapshot for " + to_string( reqId ) + " has ended." );
}

void ClientBrain::tickReqParams( int tickerId, double minTick,
                                 const std::string& bboExchange,
                                 int                snapshotPermissions )
{
    spdlog::warn( "The market data permissions level for req " +
                  to_string( tickerId ) + " is " + to_string( snapshotPermissions ) +
                  " and the minTick and BBO exchange are " + to_string( minTick ) +
                  " and " + bboExchange );
    m_bboExchange = bboExchange;
}

void ClientBrain::marketDataType( TickerId reqId, int marketDataType )
{
    spdlog::info( "The Market Data being requested by " + to_string( reqId ) +
                  " has been changed to type " + to_string( marketDataType ) );
}

void ClientBrain::historicalDataEnd( int reqId, const std::string& startDateStr,
                                     const std::string& endDateStr )
{
    spdlog::info( "HistoricalDataEnd. ReqId: " + to_string( reqId ) +
                  " - Start Date: " + startDateStr + ", End Date: " + endDateStr );
    Data->openHistRequests.erase( (long)reqId );
}

void ClientBrain::historicalTicks( int                                reqId,
                                   const std::vector<HistoricalTick>& ticks,
                                   bool                               done )
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
    Data->updateCandle( reqId, bar );
}

void ClientBrain::historicalDataUpdate( TickerId reqId, const Bar& bar )
{
    Data->updateCandle( reqId, bar );
}

/* ClientBroker Callbacks */
void ClientBrain::orderStatus( OrderId orderId, const std::string& status,
                               double filled, double remaining,
                               double avgFillPrice, int permId, int parentId,
                               double lastFillPrice, int clientId,
                               const std::string& whyHeld, double mktCapPrice )
{
    spdlog::warn( "In orderStatus. The status message is " + status );
    if( status == "ApiPending" )
    {
        // yet to be submitted to IB server, consider it open
        spdlog::info( "Received ApiPending status for order " + to_string( orderId ) );
        Broker->pendingOrders.insert( Broker->orderMap[orderId] );
    }
    else if( status == "PendingSubmit" )
    {
        // no confirmation from IB that the order has been received, consider open
        spdlog::info( "Received PendingSubmit status for order " +
                      to_string( orderId ) );
        Broker->pendingOrders.insert( Broker->orderMap[orderId] );
    }
    else if( status == "PendingCancel" )
    {
        spdlog::info( "Received PendingCancel status for order " +
                      to_string( orderId ) );
        // currently pending cancel, not handled for now
    }
    else if( status == "PreSubmitted" )
    {
        // order has been accepted by IB and is yet to be "elected"
        spdlog::info( "Received PreSubmitted status for order " +
                      to_string( orderId ) );
        Broker->pendingOrders.insert( Broker->orderMap[orderId] );
    }
    else if( status == "Submitted" )
    {
        // order has been accepted by the system
        spdlog::info( "Received Submitted status for order " + to_string( orderId ) );
        Broker->pendingOrders.erase( Broker->orderMap[orderId] );
        Broker->openOrders.insert( Broker->orderMap[orderId] );
    }
    else if( status == "ApiCancelled" )
    {
        // order has been requested to be cancelled, after being accepted but before
        // being acknowledged
        spdlog::info( "Received ApiCancelled status for order " +
                      to_string( orderId ) );
        Broker->pendingOrders.erase( Broker->orderMap[orderId] );
        Broker->openOrders.erase( Broker->orderMap[orderId] );
    }
    else if( status == "Cancelled" )
    {
        // order has been confirmed to be cancelled
        spdlog::info( "Received Cancelled status for order " + to_string( orderId ) );
        Broker->openOrders.erase( Broker->orderMap[orderId] );
    }
    else if( status == "Filled" )
    {
        // order has been completely filled
        spdlog::info( "Received Filled status for order " + to_string( orderId ) );
        auto filter = ExecutionFilter();
        filter.m_acctCode = Account->accountID;
        filter.m_clientId = clientId;
        filter.m_symbol = Broker->orderMap[orderId].first.symbol;
        filter.m_secType = Broker->orderMap[orderId].first.secType;
        filter.m_side = Broker->orderMap[orderId].second.action;
        auto newExecId = getNextReqId();
        Broker->executionMap[newExecId] = Broker->orderMap[orderId];
        Broker->filledOrder( newExecId, filter );
        Broker->openOrders.erase( Broker->orderMap[orderId] );
    }
    else if( status == "Inactive" )
    {
        // order has been received by the system but is no longer active due to
        // either rejection or cancellation
        spdlog::info( "Received Inactive status for order " + to_string( orderId ) );
        Broker->openOrders.erase( Broker->orderMap[orderId] );
    }
}

void ClientBrain::openOrder( OrderId orderId, const Contract& contract,
                             const Order& order, const OrderState& orderState )
{
    spdlog::info( "Received openOrder info for order " + to_string( order.orderId ) +
                  " of symbol " + contract.symbol );
}

void ClientBrain::openOrderEnd() { spdlog::info( "End openOrders" ); }

void ClientBrain::completedOrder( const Contract& contract, const Order& order,
                                  const OrderState& orderState )
{
    spdlog::info( "Received completed order update for orderId " +
                  to_string( order.orderId ) + " of symbol " + contract.symbol +
                  " with size " + to_string( order.totalQuantity ) );
}

void ClientBrain::completedOrdersEnd() { spdlog::info( "End completedOrders" ); }

void ClientBrain::execDetails( int reqId, const Contract& contract,
                               const Execution& execution )
{
    if( Broker->executionMap.find( reqId ) != Broker->executionMap.end() )
    {
        auto pa = Broker->executionMap[reqId];
        spdlog::info(
            "Received executions targeted at orders for position with symbol " +
            pa.first.symbol + ", secId: " + pa.first.secType );
        Account->update( Position( pa.first, pa.second, execution ) );
    }
    else
    {
        spdlog::error(
            "Received a reqId in execDetails that did not map to a position!" );
    }
}

void ClientBrain::execDetailsEnd( int reqId )
{
    spdlog::info( "End execDetails for " + to_string( reqId ) );
}