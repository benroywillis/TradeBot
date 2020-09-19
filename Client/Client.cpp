/* Copyright (C) 2019 Interactive Brokers LLC. All rights reserved. This code is
 * subject to the terms and conditions of the IB API Non-Commercial License or
 * the IB API Commercial License, as applicable. */

#include "StdAfx.h"

#include "Client.h"

#include "EClientSocket.h"
#include "EPosixClientSocketPlatform.h"

#include "AccountSummaryTags.h"
#include "AvailableAlgoParams.h"
#include "CommissionReport.h"
#include "CommonDefs.h"
#include "Contract.h"
#include "ContractSamples.h"
#include "Execution.h"
#include "FAMethodSamples.h"
#include "MarginCondition.h"
#include "Order.h"
#include "OrderSamples.h"
#include "OrderState.h"
#include "PercentChangeCondition.h"
#include "PriceCondition.h"
#include "ScannerSubscription.h"
#include "ScannerSubscriptionSamples.h"
#include "TimeCondition.h"
#include "Utils.h"
#include "VolumeCondition.h"
#include "executioncondition.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iostream>
#include <thread>

using namespace std;

namespace ClientSpace
{

    constexpr int MAINLOOPDELAY = 2;        // seconds
    constexpr int PING_DEADLINE = 2;        // seconds
    constexpr int SLEEP_BETWEEN_PINGS = 30; // seconds

    /** Organization
 * Constructor/Destructor
 * Housekeeping
 * Main Thread
 * Strategy
 * Account
 * Data
 * Broker
 * What are these?
 */

    /** Constructor/Destructor
 *
 */
    Client::Client()
        : m_osSignal( 2000 ), p_Client( make_shared<EClientSocket>( this, &m_osSignal ) ),
          p_State( make_shared<State>( CONNECT ) ), m_sleepDeadline( 0 ),
          p_OrderId( make_shared<OrderId>( 0 ) ), p_Reader( nullptr ),
          p_ExtraAuth( make_shared<bool>( false ) ) {}

    Client::~Client() = default;

    /** Functions useful for Brain
 * Connection methods
 * State control
 * Time updates
 * Server logging level
 * Server error callback
 * Windows error
 * Accounts under control
 */
    bool Client::connect( const char* host, int port, int clientId )
    {
        string hostName = !( ( host != nullptr ) && ( *host ) != 0 ) ? "127.0.0.1" : host;
        cout << "Connecting to " << hostName << ": " << port
             << " clientID: " << clientId << endl;
        bool bRes = p_Client->eConnect( host, port, clientId, *p_ExtraAuth );
        if( bRes )
        {
            cout << "Connected to " << p_Client->host().c_str() << ": "
                 << p_Client->port() << " clientID: " << clientId << endl;
            p_Reader = make_shared<EReader>( p_Client.get(), &m_osSignal );
            p_Reader->start();
        }
        else
        {
            cout << "Cannot connect to " << p_Client->host().c_str() << ": "
                 << p_Client->port() << " clientID: " << clientId << endl;
        }
        return bRes;
    }

    void Client::disconnect() const
    {
        p_Client->eDisconnect();
        cout << "Disconnected" << endl;
    }

    void Client::connectionClosed() { printf( "Connection Closed\n" ); }

    bool Client::isConnected() const { return p_Client->isConnected(); }

    void Client::connectAck()
    {
        if( !*p_ExtraAuth && p_Client->asyncEConnect() )
        {
            p_Client->startApi();
        }
    }

    void Client::setConnectOptions( const std::string& connectOptions )
    {
        p_Client->setConnectOptions( connectOptions );
    }

    void Client::currentTime( long time )
    {
        if( *p_State == PING_ACK )
        {
            auto       t = (time_t)time;
            struct tm* timeinfo = localtime( &t );
            printf( "The current date/time is: %s", asctime( timeinfo ) );

            auto now = ::time( nullptr );
            m_sleepDeadline = now + SLEEP_BETWEEN_PINGS;

            *p_State = PING_ACK;
        }
    }

    void Client::reqCurrentTime()
    {
        cout << "Requesting current time" << endl;
        // set ping deadline to "now + n seconds"
        m_sleepDeadline = time( nullptr ) + PING_DEADLINE;
        *p_State = PING_ACK;
        p_Client->reqCurrentTime();
    }

    void Client::reqHeadTimestamp()
    {
        p_Client->reqHeadTimestamp( 14001, ContractSamples::EurGbpFx(), "MIDPOINT", 1,
                                    1 );
        std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
        p_Client->cancelHeadTimestamp( 14001 );

        *p_State = REQHEADTIMESTAMP_ACK;
    }

    void Client::headTimestamp( int reqId, const std::string& headTimestamp )
    {
        printf( "Head time stamp. ReqId: %d - Head time stamp: %s,\n", reqId,
                headTimestamp.c_str() );
    }

    void Client::miscelaneous()
    {
        /*** Request TWS' current time ***/
        p_Client->reqCurrentTime();
        /*** Setting TWS logging level  ***/
        p_Client->setServerLogLevel( 5 );

        *p_State = MISCELANEOUS_ACK;
    }

    void Client::nextValidId( OrderId orderId )
    {
        printf( "Next Valid Id: %ld\n", orderId );
        *p_OrderId = orderId;
    }

    void Client::error( int id, int errorCode, const std::string& errorString )
    {
        printf( "Error. Id: %d, Code: %d, Msg: %s\n", id, errorCode,
                errorString.c_str() );
    }

    void Client::winError( const std::string& str, int lastError ) {}

    /** Main Thread
 * Loop that repeats every MAINLOOPDELAY seconds
 */
    void Client::processMessages()
    {
        time_t now = time( nullptr );
        switch( *p_State )
        {
            case PING:
                reqCurrentTime();
                break;
            case PING_ACK:
                if( m_sleepDeadline < now )
                {
                    disconnect();
                    return;
                }
                break;
            case IDLE:
                if( m_sleepDeadline < now )
                {
                    *p_State = PING;
                    return;
                }
                break;
        }

        p_Client->reqPositions();
        std::this_thread::sleep_for( std::chrono::seconds( MAINLOOPDELAY ) );
        m_osSignal.waitForSignal();
        errno = 0;
        p_Reader->processMsgs();
    }

    /** Functions useful for BTStrategy
 * Position updates
 * Account updates
 * PnL updates
 * Options operations and conditions
 * Fundamentals information
 * News research and update, current and historical
 * Order conditions
 * IB Algorithms samples
 */

    void Client::positionEnd() { cout << "PositionEnd" << endl; }

    void Client::pnlOperation()
    {
        p_Client->reqPnL( 7001, "DUD00029", "" );
        std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
        p_Client->cancelPnL( 7001 );
        *p_State = PNL_ACK;
    }

    void Client::pnlSingleOperation()
    {
        p_Client->reqPnLSingle( 7002, "DUD00029", "", 268084 );
        std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
        p_Client->cancelPnLSingle( 7002 );
        *p_State = PNLSINGLE_ACK;
    }

    void Client::optionsOperations()
    {
        p_Client->reqSecDefOptParams( 0, "IBM", "", "STK", 8314 );
        p_Client->calculateImpliedVolatility( 5001, ContractSamples::NormalOption(), 5,
                                              85, TagValueListSPtr() );
        p_Client->cancelCalculateImpliedVolatility( 5001 );

        p_Client->calculateOptionPrice( 5002, ContractSamples::NormalOption(), 0.22,
                                        85, TagValueListSPtr() );
        p_Client->cancelCalculateOptionPrice( 5002 );

        *p_State = OPTIONSOPERATIONS_ACK;
    }

    void Client::reqNewsTicks()
    {
        p_Client->reqMktData( 12001, ContractSamples::USStockAtSmart(), "mdoff,292",
                              false, false, TagValueListSPtr() );

        std::this_thread::sleep_for( std::chrono::seconds( 5 ) );

        p_Client->cancelMktData( 12001 );

        *p_State = REQNEWSTICKS_ACK;
    }

    void Client::tickNews( int tickerId, time_t timeStamp,
                           const std::string& providerCode,
                           const std::string& articleId, const std::string& headline,
                           const std::string& extraData )
    {
        // printf( "News Tick. TickerId: %d, TimeStamp: %s, ProviderCode: %s,
        // ArticleId: %s, Headline: %s, ExtraData: %s\n", tickerId, ctime( &( timeStamp
        // /= 1000 ) ), providerCode.c_str(), articleId.c_str(), headline.c_str(),
        // extraData.c_str() );
    }

    void Client::reqNewsProviders()
    {
        // Request TWS' news providers
        p_Client->reqNewsProviders();

        *p_State = NEWSPROVIDERS_ACK;
    }

    void Client::newsProviders( const std::vector<NewsProvider>& newsProviders )
    {
        printf( "News providers (%lu):\n", newsProviders.size() );

        for( unsigned int i = 0; i < newsProviders.size(); i++ )
        {
            printf( "News provider [%d] - providerCode: %s providerName: %s\n", i,
                    newsProviders[i].providerCode.c_str(),
                    newsProviders[i].providerName.c_str() );
        }
    }

    void Client::reqNewsArticle()
    {
        // Request TWS' news article
        auto list = make_shared<TagValueList>();
        list->reserve( 1 );
        // list->push_back( ( TagValueSPtr ) new TagValue( "manual", "1" ) );
        p_Client->reqNewsArticle( 12001, "MST", "MST$06f53098",
                                  TagValueListSPtr( list ) );

        *p_State = REQNEWSARTICLE_ACK;
    }

    void Client::newsArticle( int requestId, int articleType,
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
            std::vector<std::uint8_t> bytes = Utils::base64_decode( articleText );
            std::ofstream             outfile( path, std::ios::out | std::ios::binary );
            outfile.write( (const char*)bytes.data(), bytes.size() );
            printf( "Binary/pdf article was saved to: %s\n", path.c_str() );
        }
    }

    void Client::reqHistoricalNews()
    {

        auto list = make_shared<TagValueList>();
        list->reserve( 1 );
        // list->push_back( ( TagValueSPtr ) new TagValue( "manual", "1" ) );
        p_Client->reqHistoricalNews( 12001, 8314, "BZ+FLY", "", "", 5,
                                     TagValueListSPtr( list ) );

        std::this_thread::sleep_for( std::chrono::seconds( 1 ) );

        *p_State = REQHISTORICALNEWS_ACK;
    }

    void Client::historicalNews( int requestId, const std::string& time,
                                 const std::string& providerCode,
                                 const std::string& articleId,
                                 const std::string& headline )
    {
        printf( "Historical News. RequestId: %d, Time: %s, ProviderCode: %s, "
                "ArticleId: %s, Headline: %s\n",
                requestId, time.c_str(), providerCode.c_str(), articleId.c_str(),
                headline.c_str() );
    }

    void Client::historicalNewsEnd( int requestId, bool hasMore )
    {
        printf( "Historical News End. RequestId: %d, HasMore: %s\n", requestId,
                ( hasMore ? "true" : " false" ) );
    }

    void Client::fundamentals()
    {
        p_Client->reqFundamentalData( 8001, ContractSamples::USStock(),
                                      "ReportsFinSummary", TagValueListSPtr() );
        std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
        p_Client->cancelFundamentalData( 8001 );

        *p_State = FUNDAMENTALS_ACK;
    }

    void Client::fundamentalData( TickerId reqId, const std::string& data )
    {
        printf( "FundamentalData. ReqId: %ld, %s\n", reqId, data.c_str() );
    }

    void Client::bulletins()
    {
        p_Client->reqNewsBulletins( true );
        std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
        p_Client->cancelNewsBulletins();

        *p_State = BULLETINS_ACK;
    }

    void Client::updateNewsBulletin( int msgId, int msgType,
                                     const std::string& newsMessage,
                                     const std::string& originExch )
    {
        printf( "News Bulletins. %d - Type: %d, Message: %s, Exchange of Origin: %s\n",
                msgId, msgType, newsMessage.c_str(), originExch.c_str() );
    }

    void Client::conditionSamples()
    {
        Order lmt = OrderSamples::LimitOrder( "BUY", 100, 10 );
        // Order will become active if conditioning criteria is met
        /*auto priceCondition = dynamic_cast<PriceCondition*>(
  OrderSamples::Price_Condition( 208813720, "SMART", 600, false, false ) ); auto
  execCondition = dynamic_cast<ExecutionCondition*>(
  OrderSamples::Execution_Condition( "EUR.USD", "CASH", "IDEALPRO", true ) );
  auto marginCondition = dynamic_cast<MarginCondition*>(
  OrderSamples::Margin_Condition( 30, true, false ) ); auto pctChangeCondition =
  dynamic_cast<PercentChangeCondition*>(
  OrderSamples::Percent_Change_Condition( 15.0, 208813720, "SMART", true, true )
  ); auto timeCondition = dynamic_cast<TimeCondition*>(
  OrderSamples::Time_Condition( "20160118 23:59:59", true, false ) ); auto
  volumeCondition = dynamic_cast<VolumeCondition*>(
  OrderSamples::Volume_Condition( 208813720, "SMART", false, 100, true ) );

  lmt.conditions.push_back( std::shared_ptr<PriceCondition>( priceCondition ) );
  lmt.conditions.push_back( std::shared_ptr<ExecutionCondition>( execCondition )
  ); lmt.conditions.push_back( std::shared_ptr<MarginCondition>( marginCondition
  ) ); lmt.conditions.push_back( std::shared_ptr<PercentChangeCondition>(
  pctChangeCondition ) ); lmt.conditions.push_back(
  std::shared_ptr<TimeCondition>( timeCondition ) ); lmt.conditions.push_back(
  std::shared_ptr<VolumeCondition>( volumeCondition ) );
  p_Client->placeOrder( ( *p_OrderId )++, ContractSamples::USStock(), lmt );*/

        // Conditions can make the order active or cancel it. Only LMT orders can be
        // conditionally canceled.
        Order lmt2 = OrderSamples::LimitOrder( "BUY", 100, 20 );
        // The active order will be cancelled if conditioning criteria is met
        lmt2.conditionsCancelOrder = true;
        PriceCondition* priceCondition2 = dynamic_cast<PriceCondition*>(
            OrderSamples::Price_Condition( 208813720, "SMART", 600, false, false ) );
        lmt2.conditions.push_back( std::shared_ptr<PriceCondition>( priceCondition2 ) );
        p_Client->placeOrder( ( *p_OrderId )++, ContractSamples::EuropeanStock(), lmt2 );

        *p_State = CONDITIONSAMPLES_ACK;
    }

    void Client::testAlgoSamples()
    {
        Order baseOrder = OrderSamples::LimitOrder( "BUY", 1000, 1 );

        AvailableAlgoParams::FillArrivalPriceParams( baseOrder, 0.1, "Aggressive",
                                                     "09:00:00 CET", "16:00:00 CET",
                                                     true, true, 100000 );
        p_Client->placeOrder( ( *p_OrderId )++, ContractSamples::USStockAtSmart(),
                              baseOrder );

        AvailableAlgoParams::FillDarkIceParams( baseOrder, 10, "09:00:00 CET",
                                                "16:00:00 CET", true, 100000 );
        p_Client->placeOrder( ( *p_OrderId )++, ContractSamples::USStockAtSmart(),
                              baseOrder );

        // The Time Zone in "startTime" and "endTime" attributes is ignored and always
        // defaulted to GMT
        AvailableAlgoParams::FillAccumulateDistributeParams(
            baseOrder, 10, 60, true, true, 1, true, true, "20161010-12:00:00 GMT",
            "20161010-16:00:00 GMT" );
        p_Client->placeOrder( ( *p_OrderId )++, ContractSamples::USStockAtSmart(),
                              baseOrder );

        AvailableAlgoParams::FillTwapParams( baseOrder, "Marketable", "09:00:00 CET",
                                             "16:00:00 CET", true, 100000 );
        p_Client->placeOrder( ( *p_OrderId )++, ContractSamples::USStockAtSmart(),
                              baseOrder );

        AvailableAlgoParams::FillBalanceImpactRiskParams( baseOrder, 0.1, "Aggressive",
                                                          true );
        p_Client->placeOrder( ( *p_OrderId )++, ContractSamples::USStockAtSmart(),
                              baseOrder );

        AvailableAlgoParams::FillBalanceImpactRiskParams( baseOrder, 0.1, "Aggressive",
                                                          true );
        p_Client->placeOrder( ( *p_OrderId )++, ContractSamples::USStockAtSmart(),
                              baseOrder );

        AvailableAlgoParams::FillMinImpactParams( baseOrder, 0.3 );
        p_Client->placeOrder( ( *p_OrderId )++, ContractSamples::USStockAtSmart(),
                              baseOrder );

        AvailableAlgoParams::FillAdaptiveParams( baseOrder, "Normal" );
        p_Client->placeOrder( ( *p_OrderId )++, ContractSamples::USStockAtSmart(),
                              baseOrder );

        AvailableAlgoParams::FillClosePriceParams( baseOrder, 0.5, "Neutral",
                                                   "12:00:00 EST", true, 100000 );
        p_Client->placeOrder( ( *p_OrderId )++, ContractSamples::USStockAtSmart(),
                              baseOrder );

        AvailableAlgoParams::FillPctVolParams( baseOrder, 0.5, "12:00:00 EST",
                                               "14:00:00 EST", true, 100000 );
        p_Client->placeOrder( ( *p_OrderId )++, ContractSamples::USStockAtSmart(),
                              baseOrder );

        AvailableAlgoParams::FillPriceVariantPctVolParams(
            baseOrder, 0.1, 0.05, 0.01, 0.2, "12:00:00 EST", "14:00:00 EST", true,
            100000 );
        p_Client->placeOrder( ( *p_OrderId )++, ContractSamples::USStockAtSmart(),
                              baseOrder );

        AvailableAlgoParams::FillSizeVariantPctVolParams(
            baseOrder, 0.2, 0.4, "12:00:00 EST", "14:00:00 EST", true, 100000 );
        p_Client->placeOrder( ( *p_OrderId )++, ContractSamples::USStockAtSmart(),
                              baseOrder );

        AvailableAlgoParams::FillTimeVariantPctVolParams(
            baseOrder, 0.2, 0.4, "12:00:00 EST", "14:00:00 EST", true, 100000 );
        p_Client->placeOrder( ( *p_OrderId )++, ContractSamples::USStockAtSmart(),
                              baseOrder );

        AvailableAlgoParams::FillJefferiesVWAPParams(
            baseOrder, "10:00:00 EST", "16:00:00 EST", 10, 10, "Exclude_Both", 130,
            135, 1, 10, "Patience", false, "Midpoint" );
        p_Client->placeOrder( ( *p_OrderId )++, ContractSamples::JefferiesContract(),
                              baseOrder );

        AvailableAlgoParams::FillCSFBInlineParams(
            baseOrder, "10:00:00 EST", "16:00:00 EST", "Patient", 10, 20, 100,
            "Default", false, 40, 100, 100, 35 );
        p_Client->placeOrder( ( *p_OrderId )++, ContractSamples::CSFBContract(),
                              baseOrder );

        *p_State = TESTALGOSAMPLES_ACK;
    }

    /** Functions useful for BTAccount
 * Account summary request and callback
 * Contract details request and callback
 * Account detail request and callback
 * Update market book request
 * Update market book L2 request
 * Position updates
 * PnL updates
 */
    void Client::accountOperations()
    {
        p_Client->reqManagedAccts();
        std::this_thread::sleep_for( std::chrono::seconds( 2 ) );

        p_Client->reqAccountSummary( 9001, "All", AccountSummaryTags::getAllTags() );
        std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
        p_Client->reqAccountSummary( 9002, "All", "$LEDGER" );
        std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
        p_Client->reqAccountSummary( 9003, "All", "$LEDGER:EUR" );
        std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
        p_Client->reqAccountSummary( 9004, "All", "$LEDGER:ALL" );
        std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
        p_Client->cancelAccountSummary( 9001 );
        p_Client->cancelAccountSummary( 9002 );
        p_Client->cancelAccountSummary( 9003 );
        p_Client->cancelAccountSummary( 9004 );
        std::this_thread::sleep_for( std::chrono::seconds( 2 ) );

        p_Client->reqAccountUpdates( true, "U150462" );
        std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
        p_Client->reqAccountUpdates( false, "U150462" );
        std::this_thread::sleep_for( std::chrono::seconds( 2 ) );

        p_Client->reqAccountUpdatesMulti( 9002, "U150462", "EUstocks", true );
        std::this_thread::sleep_for( std::chrono::seconds( 2 ) );

        p_Client->reqPositions();
        std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
        p_Client->cancelPositions();

        p_Client->reqPositionsMulti( 9003, "U150462", "EUstocks" );

        *p_State = ACCOUNTOPERATIONS_ACK;
    }
    void Client::accountSummary( int reqId, const std::string& account,
                                 const std::string& tag, const std::string& value,
                                 const std::string& currency )
    {
        printf( "Acct Summary. ReqId: %d, Account: %s, Tag: %s, Value: %s, Currency: "
                "%s\n",
                reqId, account.c_str(), tag.c_str(), value.c_str(), currency.c_str() );
    }

    void Client::accountSummaryEnd( int reqId )
    {
        printf( "AccountSummaryEnd. Req Id: %d\n", reqId );
    }

    void Client::accountUpdateMulti( int reqId, const std::string& account,
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

    void Client::accountUpdateMultiEnd( int reqId )
    {
        printf( "Account Update Multi End. Request: %d\n", reqId );
    }

    void Client::contractOperations()
    {
        p_Client->reqContractDetails( 209, ContractSamples::EurGbpFx() );
        std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
        p_Client->reqContractDetails( 211, ContractSamples::NewsFeedForQuery() );
        p_Client->reqContractDetails( 210, ContractSamples::OptionForQuery() );
        p_Client->reqContractDetails( 212, ContractSamples::IBMBond() );
        p_Client->reqContractDetails( 213, ContractSamples::IBKRStk() );
        p_Client->reqContractDetails( 214, ContractSamples::Bond() );
        p_Client->reqContractDetails( 215, ContractSamples::FuturesOnOptions() );
        p_Client->reqContractDetails( 216, ContractSamples::SimpleFuture() );

        *p_State = CONTRACTOPERATION_ACK;
    }

    void Client::contractDetails( int                    reqId,
                                  const ContractDetails& contractDetails )
    {
        printf( "ContractDetails begin. ReqId: %d\n", reqId );
        // printContractMsg( contractDetails.contract );
        // printContractDetailsMsg( contractDetails );
        printf( "ContractDetails end. ReqId: %d\n", reqId );
    }

    void Client::contractDetailsEnd( int reqId )
    {
        printf( "ContractDetailsEnd. %d\n", reqId );
    }

    void Client::updateAccountValue( const std::string& key, const std::string& val,
                                     const std::string& currency,
                                     const std::string& accountName )
    {
        printf( "UpdateAccountValue. Key: %s, Value: %s, Currency: %s, Account Name: "
                "%s\n",
                key.c_str(), val.c_str(), currency.c_str(), accountName.c_str() );
    }

    void Client::updatePortfolio( const Contract& contract, double position,
                                  double marketPrice, double marketValue,
                                  double averageCost, double unrealizedPNL,
                                  double             realizedPNL,
                                  const std::string& accountName )
    {
        printf( "UpdatePortfolio. %s, %s @ %s: Position: %g, MarketPrice: %g, "
                "MarketValue: %g, AverageCost: %g, UnrealizedPNL: %g, RealizedPNL: "
                "%g, AccountName: %s\n",
                ( contract.symbol ).c_str(), ( contract.secType ).c_str(),
                ( contract.primaryExchange ).c_str(), position, marketPrice, marketValue,
                averageCost, unrealizedPNL, realizedPNL, accountName.c_str() );
    }

    void Client::updateAccountTime( const std::string& timeStamp )
    {
        printf( "UpdateAccountTime. Time: %s\n", timeStamp.c_str() );
    }

    void Client::accountDownloadEnd( const std::string& accountName )
    {
        printf( "Account download finished: %s\n", accountName.c_str() );
    }

    void Client::bondContractDetails( int                    reqId,
                                      const ContractDetails& contractDetails )
    {
        printf( "BondContractDetails begin. ReqId: %d\n", reqId );
        // printBondContractDetailsMsg( contractDetails );
        printf( "BondContractDetails end. ReqId: %d\n", reqId );
    }

    void Client::execDetails( int reqId, const Contract& contract,
                              const Execution& execution )
    {
        printf( "ExecDetails. ReqId: %d - %s, %s, %s - %s, %ld, %g, %d\n", reqId,
                contract.symbol.c_str(), contract.secType.c_str(),
                contract.currency.c_str(), execution.execId.c_str(), execution.orderId,
                execution.shares, execution.lastLiquidity );
    }

    void Client::execDetailsEnd( int reqId )
    {
        printf( "ExecDetailsEnd. %d\n", reqId );
    }

    void Client::updateMktDepth( TickerId id, int position, int operation, int side,
                                 double price, int size )
    {
        printf( "UpdateMarketDepth. %ld - Position: %d, Operation: %d, Side: %d, "
                "Price: %g, Size: %d\n",
                id, position, operation, side, price, size );
    }

    void Client::updateMktDepthL2( TickerId id, int position,
                                   const std::string& marketMaker, int operation,
                                   int side, double price, int size,
                                   bool isSmartDepth )
    {
        printf( "UpdateMarketDepthL2. %ld - Position: %d, Operation: %d, Side: %d, "
                "Price: %g, Size: %d, isSmartDepth: %d\n",
                id, position, operation, side, price, size, (int)isSmartDepth );
    }

    void Client::positionMulti( int reqId, const std::string& account,
                                const std::string& modelCode,
                                const Contract& contract, double pos,
                                double avgCost )
    {
        printf( "Position Multi. Request: %d, Account: %s, ModelCode: %s, Symbol: %s, "
                "SecType: %s, Currency: %s, Position: %g, Avg Cost: %g\n",
                reqId, account.c_str(), modelCode.c_str(), contract.symbol.c_str(),
                contract.secType.c_str(), contract.currency.c_str(), pos, avgCost );
    }

    void Client::positionMultiEnd( int reqId )
    {
        printf( "Position Multi End. Request: %d\n", reqId );
    }

    void Client::position( const std::string& account, const Contract& contract,
                           double position, double avgCost )
    {
        cout << "" << endl;
    }

    void Client::pnl( int reqId, double dailyPnL, double unrealizedPnL,
                      double realizedPnL )
    {
        printf(
            "PnL. ReqId: %d, daily PnL: %g, unrealized PnL: %g, realized PnL: %g\n",
            reqId, dailyPnL, unrealizedPnL, realizedPnL );
    }

    void Client::pnlSingle( int reqId, int pos, double dailyPnL,
                            double unrealizedPnL, double realizedPnL, double value )
    {
        printf( "PnL Single. ReqId: %d, pos: %d, daily PnL: %g, unrealized PnL: %g, "
                "realized PnL: %g, value: %g\n",
                reqId, pos, dailyPnL, unrealizedPnL, realizedPnL, value );
    }
    void Client::managedAccounts( const std::string& accountsList )
    {
        // occurs automatically on initial connection
        printf( "Account List: %s\n", accountsList.c_str() );
    }
    /** Functions useful for BTData
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

    void Client::tickReqParams( int tickerId, double minTick,
                                const std::string& bboExchange,
                                int                snapshotPermissions )
    {
        printf( "tickerId: %d, minTick: %g, bboExchange: %s, snapshotPermissions: %u",
                tickerId, minTick, bboExchange.c_str(), snapshotPermissions );
        m_bboExchange = bboExchange;
    }

    void Client::tickDataOperation()
    {
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
        /*
  // Each regulatory snapshot incurs a fee of 0.01 USD
  p_Client->reqMktData(1017, ContractSamples::USStock(), "", false, true,
  TagValueListSPtr());
  */
        std::this_thread::sleep_for( std::chrono::seconds( 1 ) );

        p_Client->cancelMktData( 1001 );
        p_Client->cancelMktData( 1002 );
        p_Client->cancelMktData( 1003 );
        p_Client->cancelMktData( 1014 );
        p_Client->cancelMktData( 1015 );
        p_Client->cancelMktData( 1016 );

        *p_State = TICKDATAOPERATION_ACK;
    }

    void Client::reqHistoricalTicks()
    {
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
    }

    void Client::reqTickByTickData()
    {
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
    }

    void Client::marketDepthOperations()
    {
        p_Client->reqMktDepth( 2001, ContractSamples::EurGbpFx(), 5, false,
                               TagValueListSPtr() );
        std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
        p_Client->cancelMktDepth( 2001, false );

        p_Client->reqMktDepth( 2002, ContractSamples::EuropeanStock(), 5, true,
                               TagValueListSPtr() );
        std::this_thread::sleep_for( std::chrono::seconds( 5 ) );
        p_Client->cancelMktDepth( 2002, true );

        *p_State = MARKETDEPTHOPERATION_ACK;
    }

    void Client::realTimeBars()
    {
        p_Client->reqRealTimeBars( 3001, ContractSamples::EurGbpFx(), 5, "MIDPOINT",
                                   true, TagValueListSPtr() );
        std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
        p_Client->cancelRealTimeBars( 3001 );

        *p_State = REALTIMEBARS_ACK;
    }

    void Client::realtimeBar( TickerId reqId, long time, double open, double high,
                              double low, double close, long volume, double wap,
                              int count )
    {
        printf( "RealTimeBars. %ld - Time: %ld, Open: %g, High: %g, Low: %g, Close: "
                "%g, Volume: %ld, Count: %d, WAP: %g\n",
                reqId, time, open, high, low, close, volume, count, wap );
    }

    void Client::marketDataType()
    {
        // 1 -> real-time market data
        // 2 -> frozen market data
        // 3 -> delayed market data
        // 4 -> delayed-frozen market data
        p_Client->reqMarketDataType( 2 );
        *p_State = MARKETDATATYPE_ACK;
    }

    void Client::marketDataType( TickerId reqId, int marketDataType )
    {
        printf( "MarketDataType. ReqId: %ld, Type: %d\n", reqId, marketDataType );
    }

    void Client::historicalDataRequests()
    {
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
    }

    void Client::historicalData( TickerId reqId, const Bar& bar )
    {
        printf( "HistoricalData. ReqId: %ld - Date: %s, Open: %g, High: %g, Low: %g, "
                "Close: %g, Volume: %lld, Count: %d, WAP: %g\n",
                reqId, bar.time.c_str(), bar.open, bar.high, bar.low, bar.close,
                bar.volume, bar.count, bar.wap );
    }

    void Client::historicalDataEnd( int reqId, const std::string& startDateStr,
                                    const std::string& endDateStr )
    {
        std::cout << "HistoricalDataEnd. ReqId: " << reqId
                  << " - Start Date: " << startDateStr << ", End Date: " << endDateStr
                  << std::endl;
    }

    void Client::historicalTicks( int                                reqId,
                                  const std::vector<HistoricalTick>& ticks,
                                  bool                               done )
    {
        for( HistoricalTick tick : ticks )
        {
            std::time_t t = tick.time;
            std::cout << "Historical tick. ReqId: " << reqId << ", time: " << ctime( &t )
                      << ", price: " << tick.price << ", size: " << tick.size
                      << std::endl;
        }
    }

    void Client::historicalTicksBidAsk(
        int reqId, const std::vector<HistoricalTickBidAsk>& ticks, bool done )
    {
        for( HistoricalTickBidAsk tick : ticks )
        {
            std::time_t t = tick.time;
            std::cout << "Historical tick bid/ask. ReqId: " << reqId
                      << ", time: " << ctime( &t ) << ", price bid: " << tick.priceBid
                      << ", price ask: " << tick.priceAsk
                      << ", size bid: " << tick.sizeBid
                      << ", size ask: " << tick.sizeAsk
                      << ", bidPastLow: " << tick.tickAttribBidAsk.bidPastLow
                      << ", askPastHigh: " << tick.tickAttribBidAsk.askPastHigh
                      << std::endl;
        }
    }

    void Client::historicalTicksLast( int                                    reqId,
                                      const std::vector<HistoricalTickLast>& ticks,
                                      bool                                   done )
    {
        for( const auto& tick : ticks )
        {
            std::time_t t = tick.time;
            std::cout << "Historical tick last. ReqId: " << reqId
                      << ", time: " << ctime( &t ) << ", price: " << tick.price
                      << ", size: " << tick.size << ", exchange: " << tick.exchange
                      << ", special conditions: " << tick.specialConditions
                      << ", unreported: " << tick.tickAttribLast.unreported
                      << ", pastLimit: " << tick.tickAttribLast.pastLimit << std::endl;
        }
    }

    void Client::historicalDataUpdate( TickerId reqId, const Bar& bar )
    {
        printf( "HistoricalDataUpdate. ReqId: %ld - Date: %s, Open: %g, High: %g, "
                "Low: %g, Close: %g, Volume: %lld, Count: %d, WAP: %g\n",
                reqId, bar.time.c_str(), bar.open, bar.high, bar.low, bar.close,
                bar.volume, bar.count, bar.wap );
    }

    void Client::reqMatchingSymbols()
    {
        p_Client->reqMatchingSymbols( 11001, "IBM" );
        *p_State = SYMBOLSAMPLES_ACK;
    }

    void Client::reqMktDepthExchanges()
    {
        p_Client->reqMktDepthExchanges();

        *p_State = REQMKTDEPTHEXCHANGES_ACK;
    }

    void Client::marketScanners()
    {
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
    }

    void Client::scannerParameters( const std::string& xml )
    {
        printf( "ScannerParameters. %s\n", xml.c_str() );
    }

    void Client::scannerData( int reqId, int rank,
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

    void Client::scannerDataEnd( int reqId )
    {
        printf( "ScannerDataEnd. %d\n", reqId );
    }

    void Client::reqHistogramData()
    {
        p_Client->reqHistogramData( 15001, ContractSamples::IBMUSStockAtSmart(), false,
                                    "1 weeks" );
        std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
        p_Client->cancelHistogramData( 15001 );
        *p_State = REQHISTOGRAMDATA_ACK;
    }

    void Client::histogramData( int reqId, const HistogramDataVector& data )
    {
        printf( "Histogram. ReqId: %d, data length: %lu\n", reqId, data.size() );

        for( auto item : data )
        {
            printf( "\t price: %f, size: %lld\n", item.price, item.size );
        }
    }

    void Client::tickPrice( TickerId tickerId, TickType field, double price,
                            const TickAttrib& attribs ) {}

    void Client::tickSize( TickerId tickerId, TickType field, int size ) {}

    void Client::tickOptionComputation( TickerId tickerId, TickType tickType,
                                        double impliedVol, double delta,
                                        double optPrice, double pvDividend,
                                        double gamma, double vega, double theta,
                                        double undPrice ) {}

    void Client::tickGeneric( TickerId tickerId, TickType tickType, double value ) {}

    void Client::tickString( TickerId tickerId, TickType tickType,
                             const std::string& value ) {}

    void Client::tickSnapshotEnd( int reqId ) {}

    void Client::tickEFP( TickerId tickerId, TickType tickType, double basisPoints,
                          const std::string& formattedBasisPoints,
                          double totalDividends, int holdDays,
                          const std::string& futureLastTradeDate,
                          double dividendImpact, double dividendsToLastTradeDate ) {}

    void Client::tickByTickAllLast( int reqId, int tickType, time_t time,
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

    void Client::tickByTickBidAsk( int reqId, time_t time, double bidPrice,
                                   double askPrice, int bidSize, int askSize,
                                   const TickAttribBidAsk& tickAttribBidAsk )
    {
        printf( "Tick-By-Tick. ReqId: %d, TickType: BidAsk, Time: %s, BidPrice: %g, "
                "AskPrice: %g, BidSize: %d, AskSize: %d, BidPastLow: %d, AskPastHigh: "
                "%d\n",
                reqId, ctime( &time ), bidPrice, askPrice, bidSize, askSize,
                (int)tickAttribBidAsk.bidPastLow, (int)tickAttribBidAsk.askPastHigh );
    }

    void Client::tickByTickMidPoint( int reqId, time_t time, double midPoint )
    {
        printf(
            "Tick-By-Tick. ReqId: %d, TickType: MidPoint, Time: %s, MidPoint: %g\n",
            reqId, ctime( &time ), midPoint );
    }

    void Client::securityDefinitionOptionalParameter(
        int reqId, const std::string& exchange, int underlyingConId,
        const std::string& tradingClass, const std::string& multiplier,
        const std::set<std::string>& expirations, const std::set<double>& strikes )
    {
        printf( "Security Definition Optional Parameter. Request: %d, Trading Class: "
                "%s, Multiplier: %s\n",
                reqId, tradingClass.c_str(), multiplier.c_str() );
    }

    void Client::securityDefinitionOptionalParameterEnd( int reqId )
    {
        printf( "Security Definition Optional Parameter End. Request: %d\n", reqId );
    }

    /** Functions helpful to BTBroker
 * Placing orders
 * OCA orders
 * Order status
 * Commission report
 */
    void Client::orderOperations()
    {
        // The parameter is always ignored.
        p_Client->reqIds( -1 );
        p_Client->reqAllOpenOrders();
        p_Client->reqAutoOpenOrders( true );
        p_Client->reqOpenOrders();

        // remember to ALWAYS increment the nextValidId after placing an order so it
        // can be used for the next one!
        p_Client->placeOrder( ( *p_OrderId )++, ContractSamples::USStock(),
                              OrderSamples::LimitOrder( "SELL", 1, 50 ) );

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
        p_Client->placeOrder( ( *p_OrderId )++, ContractSamples::USStockAtSmart(),
                              OrderSamples::Midprice( "BUY", 1, 150 ) );
        p_Client->placeOrder( ( *p_OrderId )++, ContractSamples::USStockAtSmart(),
                              OrderSamples::LimitOrderWithCashQty( "BUY", 1, 30, 5000 ) );

        std::this_thread::sleep_for( std::chrono::seconds( 1 ) );

        p_Client->cancelOrder( *p_OrderId - 1 );
        p_Client->reqGlobalCancel();
        p_Client->reqExecutions( 10001, ExecutionFilter() );
        p_Client->reqCompletedOrders( false );

        *p_State = ORDEROPERATIONS_ACK;
    }

    void Client::ocaSamples()
    {
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
    }

    void Client::orderStatus( OrderId orderId, const std::string& status,
                              double filled, double remaining, double avgFillPrice,
                              int permId, int parentId, double lastFillPrice,
                              int clientId, const std::string& whyHeld,
                              double mktCapPrice )
    {
        printf( "OrderStatus. Id: %ld, Status: %s, Filled: %g, Remaining: %g, "
                "AvgFillPrice: %g, PermId: %d, LastFillPrice: %g, ClientId: %d, "
                "WhyHeld: %s, MktCapPrice: %g\n",
                orderId, status.c_str(), filled, remaining, avgFillPrice, permId,
                lastFillPrice, clientId, whyHeld.c_str(), mktCapPrice );
    }

    void Client::openOrder( OrderId orderId, const Contract& contract,
                            const Order& order, const OrderState& orderState )
    {
        printf( "OpenOrder. PermId: %d, ClientId: %ld, OrderId: %ld, Account: %s, "
                "Symbol: %s, SecType: %s, Exchange: %s:, Action: %s, OrderType:%s, "
                "TotalQty: %g, CashQty: %g, "
                "LmtPrice: %g, AuxPrice: %g, Status: %s\n",
                order.permId, order.clientId, orderId, order.account.c_str(),
                contract.symbol.c_str(), contract.secType.c_str(),
                contract.exchange.c_str(), order.action.c_str(),
                order.orderType.c_str(), order.totalQuantity,
                order.cashQty == UNSET_DOUBLE ? 0 : order.cashQty, order.lmtPrice,
                order.auxPrice, orderState.status.c_str() );
    }

    void Client::openOrderEnd() { printf( "OpenOrderEnd\n" ); }

    void Client::completedOrder( const Contract& contract, const Order& order,
                                 const OrderState& orderState )
    {
        printf(
            "CompletedOrder. PermId: %ld, ParentPermId: %lld, Account: %s, Symbol: "
            "%s, SecType: %s, Exchange: %s:, Action: %s, OrderType: %s, TotalQty: "
            "%g, CashQty: %g, FilledQty: %g, "
            "LmtPrice: %g, AuxPrice: %g, Status: %s, CompletedTime: %s, "
            "CompletedStatus: %s\n",
            order.permId, order.parentPermId == UNSET_LONG ? 0 : order.parentPermId,
            order.account.c_str(), contract.symbol.c_str(), contract.secType.c_str(),
            contract.exchange.c_str(), order.action.c_str(), order.orderType.c_str(),
            order.totalQuantity, order.cashQty == UNSET_DOUBLE ? 0 : order.cashQty,
            order.filledQuantity, order.lmtPrice, order.auxPrice,
            orderState.status.c_str(), orderState.completedTime.c_str(),
            orderState.completedStatus.c_str() );
    }

    void Client::completedOrdersEnd() { printf( "CompletedOrdersEnd\n" ); }

    void Client::commissionReport( const CommissionReport& commissionReport )
    {
        printf( "CommissionReport. %s - %g %s RPNL %g\n",
                commissionReport.execId.c_str(), commissionReport.commission,
                commissionReport.currency.c_str(), commissionReport.realizedPNL );
    }

    /** What are these?
 *
 */
    void Client::bracketSample()
    {
        // TODO: read about what this is
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
    }

    void Client::hedgeSample()
    {
        // F Hedge order
        // Parent order on a contract which currency differs from your base currency
        Order parent = OrderSamples::LimitOrder( "BUY", 100, 10 );
        parent.orderId = ( *p_OrderId )++;
        parent.transmit = false;
        // Hedge on the currency conversion
        Order hedge = OrderSamples::MarketFHedge( parent.orderId, "BUY" );
        // Place the parent first...
        p_Client->placeOrder( parent.orderId, ContractSamples::EuropeanStock(),
                              parent );
        // Then the hedge order
        p_Client->placeOrder( ( *p_OrderId )++, ContractSamples::EurGbpFx(), hedge );

        *p_State = HEDGESAMPLES_ACK;
    }

    void Client::rerouteCFDOperations()
    {
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
    }

    void Client::marketRuleOperations()
    {
        p_Client->reqContractDetails( 17001, ContractSamples::IBMBond() );
        p_Client->reqContractDetails( 17002, ContractSamples::IBKRStk() );

        std::this_thread::sleep_for( std::chrono::seconds( 2 ) );

        p_Client->reqMarketRule( 26 );
        p_Client->reqMarketRule( 635 );
        p_Client->reqMarketRule( 1388 );

        *p_State = MARKETRULE_ACK;
    }

    void Client::continuousFuturesOperations()
    {
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
    }

    void Client::whatIfSamples()
    {
        p_Client->placeOrder( ( *p_OrderId )++, ContractSamples::USStockAtSmart(),
                              OrderSamples::WhatIfLimitOrder( "BUY", 200, 120 ) );

        *p_State = WHATIFSAMPLES_ACK;
    }

    /** What are these?
 *
 */
    void Client::deltaNeutralValidation(
        int reqId, const DeltaNeutralContract& deltaNeutralContract )
    {
        printf( "DeltaNeutralValidation. %d, ConId: %ld, Delta: %g, Price: %g\n",
                reqId, deltaNeutralContract.conId, deltaNeutralContract.delta,
                deltaNeutralContract.price );
    }

    void Client::verifyMessageAPI( const std::string& apiData )
    {
        printf( "verifyMessageAPI: %s\b", apiData.c_str() );
    }

    void Client::verifyCompleted( bool isSuccessful, const std::string& errorText )
    {
        printf( "verifyCompleted. IsSuccessfule: %d - Error: %s\n", (int)isSuccessful,
                errorText.c_str() );
    }

    void Client::verifyAndAuthMessageAPI( const std::string& apiDatai,
                                          const std::string& xyzChallenge )
    {
        printf( "verifyAndAuthMessageAPI: %s %s\n", apiDatai.c_str(),
                xyzChallenge.c_str() );
    }

    void Client::verifyAndAuthCompleted( bool               isSuccessful,
                                         const std::string& errorText )
    {
        printf( "verifyAndAuthCompleted. IsSuccessful: %d - Error: %s\n",
                (int)isSuccessful, errorText.c_str() );
        if( isSuccessful )
        {
            p_Client->startApi();
        }
    }

    void Client::displayGroupList( int reqId, const std::string& groups )
    {
        printf( "Display Group List. ReqId: %d, Groups: %s\n", reqId, groups.c_str() );
    }

    void Client::displayGroupUpdated( int reqId, const std::string& contractInfo )
    {
        std::cout << "Display Group Updated. ReqId: " << reqId
                  << ", Contract Info: " << contractInfo << std::endl;
    }

    void Client::softDollarTiers( int                                reqId,
                                  const std::vector<SoftDollarTier>& tiers )
    {
        printf( "Soft dollar tiers (%lu):", tiers.size() );

        for( const auto& index : tiers )
        {
            printf( "%s\n", index.displayName().c_str() );
        }
    }

    void Client::familyCodes( const std::vector<FamilyCode>& familyCodes )
    {
        printf( "Family codes (%lu):\n", familyCodes.size() );

        for( unsigned int i = 0; i < familyCodes.size(); i++ )
        {
            printf( "Family code [%d] - accountID: %s familyCodeStr: %s\n", i,
                    familyCodes[i].accountID.c_str(),
                    familyCodes[i].familyCodeStr.c_str() );
        }
    }

    void Client::symbolSamples(
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

    void Client::mktDepthExchanges(
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

    void Client::smartComponents( int reqId, const SmartComponentsMap& theMap )
    {
        printf( "Smart components: (%lu):\n", theMap.size() );

        for( const auto& i : theMap )
        {
            printf( " bit number: %d exchange: %s exchange letter: %c\n", i.first,
                    std::get<0>( i.second ).c_str(), std::get<1>( i.second ) );
        }
    }

    void Client::rerouteMktDataReq( int reqId, int conid,
                                    const std::string& exchange )
    {
        printf( "Re-route market data request. ReqId: %d, ConId: %d, Exchange: %s\n",
                reqId, conid, exchange.c_str() );
    }

    void Client::rerouteMktDepthReq( int reqId, int conid,
                                     const std::string& exchange )
    {
        printf( "Re-route market depth request. ReqId: %d, ConId: %d, Exchange: %s\n",
                reqId, conid, exchange.c_str() );
    }

    void Client::marketRule( int                                marketRuleId,
                             const std::vector<PriceIncrement>& priceIncrements )
    {
        printf( "Market Rule Id: %d\n", marketRuleId );
        for( const auto& index : priceIncrements )
        {
            printf( "Low Edge: %g, Increment: %g\n", index.lowEdge, index.increment );
        }
    }

    void Client::orderBound( long long orderId, int apiClientId, int apiOrderId )
    {
        printf( "Order bound. OrderId: %lld, ApiClientId: %d, ApiOrderId: %d\n",
                orderId, apiClientId, apiOrderId );
    }

    void Client::receiveFA( faDataType pFaDataType, const std::string& cxml )
    {
        std::cout << "Receiving FA: " << (int)pFaDataType << std::endl
                  << cxml << std::endl;
    }

} // namespace ClientSpace