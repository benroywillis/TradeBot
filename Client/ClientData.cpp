#include "ClientData.h"
#include "DataArray.h"
#include "EClientSocket.h"
#include "Indicator.h"
#include "SMA.h"
#include "TimeStamp.h"
#include "bar.h"
#include <chrono>
#include <iostream>
#include <spdlog/spdlog.h>
#include <thread>

constexpr int64_t TIMEOUT = 20;

using namespace std;
using namespace ClientSpace;

ClientData::ClientData()
{
    indicators = vector<BTIndicator*>();
    openHistRequests = set<long>();
    openDataLines = set<long>();
    updatedLines = set<long>();
    TimeLine = TimeMap();
    valid = false;
}

ClientData::ClientData( vector<BTIndicator*>& newIndicators )
{
    indicators = newIndicators;
    openHistRequests = set<long>();
    openDataLines = set<long>();
    updatedLines = set<long>();
    TimeLine = TimeMap();
    valid = false;
}

ClientData::ClientData( const shared_ptr<EClientSocket>& newClient,
                        const shared_ptr<State>&         newState )
{
    indicators = vector<BTIndicator*>();
    p_Client = newClient;
    p_State = newState;
    openHistRequests = set<long>();
    openDataLines = set<long>();
    updatedLines = set<long>();
    TimeLine = TimeMap();
    valid = false;
}

void ClientData::addClient( std::shared_ptr<EClientSocket> newClient )
{
    p_Client = move( newClient );
}
void ClientData::addState( std::shared_ptr<ClientSpace::State> newState )
{
    p_State = move( newState );
}

void ClientData::init()
{
    initContractVectors();
    startLiveData();
    valid = true;
}

void ClientData::startLiveData()
{
    // market data lines
    for( auto& con : stockContracts )
    {
        newLiveRequest( con, getNextVectorId() );
        this_thread::sleep_for( chrono::milliseconds( 500 ) );
    }
    for( auto& con : optionContracts )
    {
        newLiveRequest( con, getNextVectorId() );
        this_thread::sleep_for( chrono::milliseconds( 500 ) );
    }
}

void ClientData::harvest( int index )
{
    // historical data requests
    if( index == 0 )
    {
        for( auto& con : stockContracts )
        {
            spdlog::info( "Retrieving historical data of index 0 for " + con.symbol );
            newHistRequest( con, getNextVectorId(), "1800 S", "1 secs", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( 200 ) );
            newHistRequest( con, getNextVectorId(), "3600 S", "5 secs", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( 200 ) );
            newHistRequest( con, getNextVectorId(), "14400 S", "10 secs", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( 200 ) );
            newHistRequest( con, getNextVectorId(), "14400 S", "15 secs", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( 200 ) );
            newHistRequest( con, getNextVectorId(), "28800 S", "30 secs", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( 200 ) );
            newHistRequest( con, getNextVectorId(), "1 D", "1 min",
                            "TRADES" ); // the length of all candles north of 30s have
                                        // no length restriction anymore
            this_thread::sleep_for( chrono::milliseconds( 200 ) );
        }
        *p_State = DATAHARVEST_TIMEOUT_0;
        startTimer();
    }
    else if( index == 1 )
    {
        for( auto& con : stockContracts )
        {
            spdlog::info( "Retrieving historical data of index 1 for " + con.symbol );
            newHistRequest( con, getNextVectorId(), "2 D", "2 mins", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( 200 ) );
            newHistRequest( con, getNextVectorId(), "1 W", "3 mins", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( 200 ) );
            newHistRequest( con, getNextVectorId(), "1 W", "5 mins", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( 200 ) );
            newHistRequest( con, getNextVectorId(), "1 W", "10 mins", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( 200 ) );
            newHistRequest( con, getNextVectorId(), "1 W", "15 mins", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( 200 ) );
            newHistRequest( con, getNextVectorId(), "1 W", "20 mins", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( 200 ) );
        }
        *p_State = DATAHARVEST_TIMEOUT_1;
        startTimer();
    }
    else if( index == 2 )
    {
        for( auto& con : stockContracts )
        {
            spdlog::info( "Retrieving historical data of index 2 for " + con.symbol );
            newHistRequest( con, getNextVectorId(), "1 M", "30 mins", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( 200 ) );
            newHistRequest( con, getNextVectorId(), "1 M", "1 hour", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( 200 ) );
            newHistRequest( con, getNextVectorId(), "1 M", "2 hours", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( 200 ) );
            newHistRequest( con, getNextVectorId(), "1 M", "3 hours", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( 200 ) );
            newHistRequest( con, getNextVectorId(), "1 M", "4 hours", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( 200 ) );
            newHistRequest( con, getNextVectorId(), "1 M", "8 hours", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( 200 ) );
        }
        *p_State = DATAHARVEST_TIMEOUT_2;
        startTimer();
    }
    else if( index == 3 )
    {
        for( auto& con : stockContracts )
        {
            spdlog::info( "Retrieving historical data of index 3 for " + con.symbol );
            newHistRequest( con, getNextVectorId(), "1 Y", "1 day", "TRADES" );
            this_thread::sleep_for( chrono::milliseconds( 500 ) );
            newLiveRequest( con, getNextVectorId() );
            this_thread::sleep_for( chrono::milliseconds( 500 ) );
        }
        *p_State = DATAHARVEST_LIVE;
    }
}

void ClientData::newLiveRequest( Contract& con, long vecId )
{
    auto newVec = make_shared<DataArray>( vecId, con.conId, con.symbol, con.secId,
                                          con.secType, con.exchange, con.currency );
    openDataLines.insert( vecId );
    if( con.secType == "STK" )
    {
        snapMap[vecId] = SnapHold();
    }
    else if( con.secType == "OPT" )
    {
        newVec->contract.lastTradeDateOrContractMonth =
            con.lastTradeDateOrContractMonth;
        newVec->contract.strike = con.strike;
        newVec->contract.right = con.right;
        newVec->exprDate = TimeStamp( con.lastTradeDateOrContractMonth, true );
        optionMap[vecId] = OptionHold();
    }
    for( const auto& ind : indicators )
    {
        newVec->addIndicator( ind );
    }
    DataArrays.insert( newVec );
    p_Client->reqMktData( vecId, con, "", false, false, TagValueListSPtr() );
}

void ClientData::newHistRequest( Contract& con, long vecId, const string& length,
                                 const string& barlength, const string& type )
{
    auto   newVec = make_shared<DataArray>( vecId, con.conId, con.symbol, con.secId,
                                          con.secType, con.exchange, con.currency );
    string inter = barlength;
    inter.erase( remove_if( inter.begin(), inter.end(),
                            []( unsigned char c ) { return std::isspace( c ); } ),
                 inter.end() );
    newVec->interval = inter;
    DataArrays.insert( newVec );
    openHistRequests.insert( vecId );
    p_Client->reqHistoricalData( vecId, con, "", length, barlength, type, 1, 1,
                                 false, TagValueListSPtr() );
}

void ClientData::startTimer() { start = chrono::high_resolution_clock::now(); }

bool ClientData::checkTimer()
{
    auto nowTime = chrono::_V2::system_clock::now();
    auto intDuration = chrono::duration_cast<chrono::seconds>( nowTime - start );
    spdlog::info( "Seconds until next data harvest: " +
                  to_string( TIMEOUT - intDuration.count() ) );
    return chrono::duration_cast<chrono::seconds>(
               chrono::_V2::system_clock::now() - start )
               .count() > TIMEOUT;
}

void ClientData::updateCandle( TickerId reqId, const Bar& bar )
{
    auto point = DataArrays.find( reqId );
    if( point != DataArrays.end() )
    {
        CandleStruct newPoint;
        newPoint.time = TimeStamp( bar.time );
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

void ClientData::updatePrice( TickerId reqId, SnapStruct& newPoint, double price,
                              int field )
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

void ClientData::updatePrice( TickerId reqId, OptionStruct& newPoint,
                              double price, int field )
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

void ClientData::updateSize( TickerId reqId, SnapStruct& newPoint, int value,
                             int field )
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

void ClientData::updateSize( TickerId reqId, OptionStruct& newPoint, int value,
                             int field )
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

void ClientData::updateOptionGreeks( TickerId reqId, OptionStruct& newPoint,
                                     double impliedVol, double delta,
                                     double optPrice, double pvDividend,
                                     double gamma, double vega, double theta,
                                     int field )
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

void ClientData::addPoint( long reqId, SnapStruct newPoint )
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

void ClientData::addPoint( long reqId, OptionStruct newPoint )
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

void ClientData::updateTimeLine( const shared_ptr<DataArray>& vec )
{
    auto last = vec->getLastPoint();
    if( last )
    {
        auto it = *last;
        if( TimeLine.find( it->getTime() ) == TimeLine.end() )
        {
            TimeLine[it->getTime()] = GlobalTimePoint( vec, it );
            BTData::currentTime = prev( TimeLine.end() );
        }
        else
        {
            TimeLine[it->getTime()].addVector( vec, it );
        }
    }
    updatedLines.insert( vec->vectorId );
}

void ClientData::initContractVectors()
{
    // stock tickers
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
    OPT0.lastTradeDateOrContractMonth = "20200731";
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
    OPT6.lastTradeDateOrContractMonth = "20200731";
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
}

bool ClientData::updated()
{
    if( !updatedLines.empty() )
    {
        updatedLines.clear();
        return true;
    }
    return false;
}

bool ClientData::updated() const { return openDataLines == updatedLines; }