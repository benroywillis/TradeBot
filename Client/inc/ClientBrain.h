#pragma once
#include "Client.h"
#include "TradeBase/Brain.h"

class ClientData;

/// Global flag indicating a keyboard interrupt
extern bool inter;

/// timeout period at the end of the ClientBrain state table, in milliseconds
constexpr int MAINLOOPDELAY = 100;
/// Maximum amount of time, in milliseconds, that a ping is allowed to be
/// unanswered
constexpr int PING_DEADLINE = 2000;
/// Timeout period between pings, in seconds
constexpr int SLEEP_BETWEEN_PINGS = 30;
/// Maximum allowable open historical data requests before starting another
/// request batch
constexpr int MAXIMUM_OPEN_HISTREQ = 5;
/// Maximum allowable open historical data requests
constexpr int MAXIMUM_HISTREQ_BUFFER_SIZE = 50;
/// Maximum allowable open market data lines
constexpr int MAXIMUM_DATALINES_BUFFER_SIZE = 100;

/// @brief Brain for the automated trader
///
/// This class handles the connection and callbacks for all operations in the
/// trading bot. Callback functions must be defined in here.
class ClientBrain : public ClientSpace::Client
{
public:
    ClientBrain();
    ClientBrain( std::shared_ptr<TradeBase::BTStrategy> );
    ClientBrain( std::shared_ptr<ClientData> newData, const std::shared_ptr<TradeBase::BTStrategy>& newStrategy );
    ~ClientBrain() = default;

    void setConnectOptions( const std::string& );
    void processMessages();
    bool connect( const char* host, int port, int clientId = 0 );
    void disconnect() const;
    bool isConnected() const;
    /// Initializes all members: Account (and more to come)
    void init();

private:
    std::shared_ptr<ClientData>            Data;
    std::shared_ptr<TradeBase::BTStrategy> Strategy;
    void                                   connectionClosed();
    void                                   connectAck();
    void                                   reqHeadTimestamp();
    long                                   getNextReqId();
    long                                   reqId;
    long                                   clientID;
    /// Callback to reqHeadTimestamp
    void headTimestamp( int, const std::string& );
    void reqCurrentTime();
    /// Callback for reqCurrentTime
    void currentTime( long );
    /// Automatically called when a connection is first established.
    void managedAccounts( const std::string& );
    /// Automatically called when a connection is first established.
    void nextValidId( OrderId );
    /// @brief Triggered whenever an error or update is thrown from the Host.
    ///
    /// For example, when a market data error has occurred, this function is
    /// called. Also, this serves as a callback to whenever subscriptions are
    /// cancelled.
    void error( int, int, const std::string& );
    /// Unneeded for now.
    void winError( const std::string&, int );

    /* Callbacks for ClientAccount */
    /// Callback for reqAccountSummary for all account related information
    void accountSummary( int, const std::string&, const std::string&, const std::string&, const std::string& );
    /// @brief Called whenever a position or account value changes, or every 3
    /// minutes.
    ///
    /// This callback is triggered every time an account update from the
    /// reqAccountUpdates subscription is received. These account updates occur
    /// either when a position or account value is updated, or at most every 3
    /// minutes.
    void updateAccountValue( const std::string&, const std::string&, const std::string&, const std::string& );
    /// @brief Called whenever a position in the subscribed account changes.
    ///
    /// This callback is triggered every time an account update from the
    /// reqAccountUpdates subscription is received. These account updates occur
    /// either when a position or account value is updated, or at most every 3
    /// minutes.
    void updatePortfolio( const Contract&, double, double, double, double, double, double, const std::string& );
    /// @brief One of three callbacks from account updates. Receives the timestamp
    /// of the last account update.
    ///
    /// This callback is triggered every time an account update from the
    /// reqAccountUpdates subscription is received. These account updates occur
    /// either when a position or account value is updated, or at most every 3
    /// minutes.
    void updateAccountTime( const std::string& );
    /// Notifies when all account information has been received after subscribing
    /// to account updates.
    void accountDownloadEnd( const std::string& );
    void position( const std::string&, const Contract&, double, double );
    void positionEnd();

    /* Callbacks for ClientData */
    void tickPrice( TickerId, TickType, double, const TickAttrib& );
    void tickSize( TickerId, TickType, int );
    void tickOptionComputation( TickerId, TickType, double, double, double, double, double, double, double, double );
    void tickGeneric( TickerId, TickType, double );
    void tickString( TickerId, TickType, const std::string& );
    void tickSnapshotEnd( int );
    void tickReqParams( int, double, const std::string&, int );
    /// @brief Automatic callback from reqMktData() to specify the type of data
    /// that will (or has already) come over the wire.
    ///
    /// 1 -> Real-time market data
    /// 2 -> "Frozen" data, or the data that was last recorded by IB before market
    /// close. 3 -> Delayed market data by 15-20 minutes 4 -> Delayed-frozen
    void marketDataType( TickerId, int );
    void historicalData( TickerId, const Bar& );
    void historicalDataEnd( int, const std::string&, const std::string& );
    void historicalTicks( int, const std::vector<HistoricalTick>&, bool );
    void historicalTicksBidAsk( int, const std::vector<HistoricalTickBidAsk>&, bool );
    void historicalTicksLast( int, const std::vector<HistoricalTickLast>&, bool );
    void historicalDataUpdate( TickerId, const Bar& );

    /* Callbacks for ClientBroker */
    /// Gives up-to-date information about each order every time its state
    /// changes. Callback from EClientSocket::placeOrder
    void orderStatus( OrderId, const std::string&, double, double, double, int, int, double, int, const std::string&, double );
    /// Receives all open orders' information. Callback from
    /// EClientSocket::reqAllOpenOrders. Does not create a subscription to open
    /// order updates
    void openOrder( OrderId, const Contract&, const Order&, const OrderState& );
    /// Notifies end of open order updates
    void openOrderEnd();
    /// Receives all completed orders. Callback from
    /// EClientSocket::reqCompletedOrders
    void completedOrder( const Contract&, const Order&, const OrderState& );
    /// Notifies end of completed order updates
    void completedOrdersEnd();
    void execDetails( int, const Contract&, const Execution& );
    void execDetailsEnd( int );
};