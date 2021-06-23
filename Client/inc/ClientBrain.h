#pragma once
#include "TradeBase/Brain.h"
#include "TradeBase/DataTypes.h"
#include "TradeBase/Strategy.h"
#include "twsapi/EReader.h"
#include "twsapi/EReaderOSSignal.h"
#include "twsapi/EWrapper.h"
#include "twsapi/Order.h"
#include <array>
#include <memory>
#include <set>
#include <vector>

class ClientData;
class EClientSocket;
struct ExecutionFilter;

/// Global flag indicating a keyboard interrupt
extern bool inter;
/// timeout period at the end of the ClientBrain state table, in milliseconds
constexpr int MAINLOOPDELAY = 100;
/// Maximum amount of time, in milliseconds, that a ping is allowed to be unanswered
constexpr int PING_DEADLINE = 2000;
/// Timeout period between pings, in seconds
constexpr int SLEEP_BETWEEN_PINGS = 30;
/// Maximum allowable open historical data requests before starting another request batch
constexpr int MAXIMUM_OPEN_HISTREQ = 5;
/// Maximum allowable open historical data requests
constexpr int MAXIMUM_HISTREQ_BUFFER_SIZE = 50;
/// Maximum allowable open market data lines
constexpr int MAXIMUM_DATALINES_BUFFER_SIZE = 100;
/// Timeout period for new data requests
constexpr int64_t TIMEOUT = 20;
/// Initialization string for the account update initialization
constexpr char AllAccountTags[] = "AccountType,NetLiquidation,TotalCashValue,SettledCash,AccruedCash,BuyingPower,EquityWithLoanValue,PreviousEquityWithLoanValue,GrossPositionValue,ReqTEquity,ReqTMargin,SMA,InitMarginReq,MaintMarginReq,AvailableFunds,ExcessLiquidity,Cushion,FullInitMarginReq,FullMaintMarginReq,FullAvailableFunds,FullExcessLiquidity,LookAheadNextChange,LookAheadInitMarginReq,LookAheadMaintMarginReq,LookAheadAvailableFunds,LookAheadExcessLiquidity,HighestSeverity,DayTradesRemaining,Leverage";

struct SnapHold
{
    TradeBase::SnapStruct bidAsk;
    TradeBase::SnapStruct lastTrade;
};

struct OptionHold
{
    TradeBase::OptionStruct bidAsk;
    TradeBase::OptionStruct lastTrade;
};

struct FutureHold
{
    TradeBase::SnapStruct bidAsk;
    TradeBase::SnapStruct lastTrade;
};

/// @brief Enumerates all states the Client can be in
///
/// The contributions from the IB API are merely for reference at this point.
/// States used to be preceded by ST_, changed by TM.
enum class State
{
    // TradeManager Contribution
    CONNECT,             // attempting a new connection, 0
    CONNECTSUCCESS,      // used just after a successful connection, 1
    CONNECTFAIL,         // a connection attempt has just failed, 2
    DISCONNECTED,        // connection has been broken after initialization
    INIT,                // A new connection has just been established and the trading bot needs to be initialized
    INITSUCCESS,         // Trading bot is fully and successfully initialized and ready to trade
    INITFAIL,            // Something went wrong in the initialization function, leads to immediate disconnect and termination of the program
    ACTIVETRADING,       // The trading bot is actively processing data and placing trades
    PASSIVETRADING,      // The trading bot is actively processing data but not placing orders
    ACCOUNTID,           // Waiting to get account ID string. Required to initialize account
    ACCOUNTIDSUCCESS,    // Successfully received account ID string from TWS
    ACCOUNTINIT,         // ClientAccount is being initialized by TWS
    ACCOUNTINITSUCCESS,  // ClientAccount has just been fully and successfully initialized
    ACCOUNTCLOSE,        // ClientAccount is closing its subscriptions
    ACCOUNTCLOSESUCCESS, // ClientAccount has successfully closed its account subscriptions
    ACCOUNTCLOSEFAIL,    // An error occurred when trying to close account subscriptions
    DATAINIT,
    DATAINITSUCCESS,
    DATAHARVEST,
    DATAHARVEST_TIMEOUT_0,
    DATAHARVEST_TIMEOUT_1,
    DATAHARVEST_TIMEOUT_2,
    DATAHARVEST_LIVE,
    DATAHARVEST_DONE,
    DATA_NEXT,
    TRADING,
    ORDERING,
    IDLE,
    INT,
    PING,
    PING_ACK,
    // IB Crap
    TICKDATAOPERATION,
    TICKDATAOPERATION_ACK,
    TICKOPTIONCOMPUTATIONOPERATION,
    TICKOPTIONCOMPUTATIONOPERATION_ACK,
    DELAYEDTICKDATAOPERATION,
    DELAYEDTICKDATAOPERATION_ACK,
    MARKETDEPTHOPERATION,
    MARKETDEPTHOPERATION_ACK,
    REALTIMEBARS,
    REALTIMEBARS_ACK,
    MARKETDATATYPE,
    MARKETDATATYPE_ACK,
    HISTORICALDATAREQUESTS,
    HISTORICALDATAREQUESTS_ACK,
    OPTIONSOPERATIONS,
    OPTIONSOPERATIONS_ACK,
    CONTRACTOPERATION,
    CONTRACTOPERATION_ACK,
    MARKETSCANNERS,
    MARKETSCANNERS_ACK,
    FUNDAMENTALS,
    FUNDAMENTALS_ACK,
    BULLETINS,
    BULLETINS_ACK,
    ACCOUNTOPERATIONS,
    ACCOUNTOPERATIONS_ACK,
    ORDEROPERATIONS,
    ORDEROPERATIONS_ACK,
    OCASAMPLES,
    OCASAMPLES_ACK,
    CONDITIONSAMPLES,
    CONDITIONSAMPLES_ACK,
    BRACKETSAMPLES,
    BRACKETSAMPLES_ACK,
    HEDGESAMPLES,
    HEDGESAMPLES_ACK,
    TESTALGOSAMPLES,
    TESTALGOSAMPLES_ACK,
    FAORDERSAMPLES,
    FAORDERSAMPLES_ACK,
    FAOPERATIONS,
    FAOPERATIONS_ACK,
    DISPLAYGROUPS,
    DISPLAYGROUPS_ACK,
    MISCELANEOUS,
    MISCELANEOUS_ACK,
    CANCELORDER,
    CANCELORDER_ACK,
    FAMILYCODES,
    FAMILYCODES_ACK,
    SYMBOLSAMPLES,
    SYMBOLSAMPLES_ACK,
    REQMKTDEPTHEXCHANGES,
    REQMKTDEPTHEXCHANGES_ACK,
    REQNEWSTICKS,
    REQNEWSTICKS_ACK,
    REQSMARTCOMPONENTS,
    REQSMARTCOMPONENTS_ACK,
    NEWSPROVIDERS,
    NEWSPROVIDERS_ACK,
    REQNEWSARTICLE,
    REQNEWSARTICLE_ACK,
    REQHISTORICALNEWS,
    REQHISTORICALNEWS_ACK,
    REQHEADTIMESTAMP,
    REQHEADTIMESTAMP_ACK,
    REQHISTOGRAMDATA,
    REQHISTOGRAMDATA_ACK,
    REROUTECFD,
    REROUTECFD_ACK,
    MARKETRULE,
    MARKETRULE_ACK,
    PNL,
    PNL_ACK,
    PNLSINGLE,
    PNLSINGLE_ACK,
    CONTFUT,
    CONTFUT_ACK,
    REQHISTORICALTICKS,
    REQHISTORICALTICKS_ACK,
    REQTICKBYTICKDATA,
    REQTICKBYTICKDATA_ACK,
    WHATIFSAMPLES,
    WHATIFSAMPLES_ACK
};

std::array<std::string, 115> StateMap = {
    // TradeManager Contribution (31)
    "CONNECT",             // attempting a new connection, 0
    "CONNECTSUCCESS",      // used just after a successful connection, 1
    "CONNECTFAIL",         // a connection attempt has just failed, 2
    "DISCONNECTED",        // connection has been broken after initialization
    "INIT",                // A new connection has just been established and the trading bot needs to be initialized
    "INITSUCCESS",         // Trading bot is fully and successfully initialized and ready to trade
    "INITFAIL",            // Something went wrong in the initialization function, leads to immediate disconnect and termination of the program
    "ACTIVETRADING",       // The trading bot is actively processing data and placing trades
    "PASSIVETRADING",      // The trading bot is actively processing data but not placing orders
    "ACCOUNTID",           // Waiting to get account ID string. Required to initialize account
    "ACCOUNTIDSUCCESS",    // Successfully received account ID string from TWS
    "ACCOUNTINIT",         // ClientAccount is being initialized by TWS
    "ACCOUNTINITSUCCESS",  // ClientAccount has just been fully and successfully initialized
    "ACCOUNTCLOSE",        // ClientAccount is closing its subscriptions
    "ACCOUNTCLOSESUCCESS", // ClientAccount has successfully closed its account subscriptions
    "ACCOUNTCLOSEFAIL",    // An error occurred when trying to close account subscriptions
    "DATAINIT",
    "DATAINITSUCCESS",
    "DATAHARVEST",
    "DATAHARVEST_TIMEOUT_0",
    "DATAHARVEST_TIMEOUT_1",
    "DATAHARVEST_TIMEOUT_2",
    "DATAHARVEST_LIVE",
    "DATAHARVEST_DONE",
    "DATA_NEXT",
    "TRADING",
    "ORDERING",
    "IDLE",
    "INT",
    "PING",
    "PING_ACK",
    // IB Crap (84)
    "TICKDATAOPERATION",
    "TICKDATAOPERATION_ACK",
    "TICKOPTIONCOMPUTATIONOPERATION",
    "TICKOPTIONCOMPUTATIONOPERATION_ACK",
    "DELAYEDTICKDATAOPERATION",
    "DELAYEDTICKDATAOPERATION_ACK",
    "MARKETDEPTHOPERATION",
    "MARKETDEPTHOPERATION_ACK",
    "REALTIMEBARS",
    "REALTIMEBARS_ACK",
    "MARKETDATATYPE",
    "MARKETDATATYPE_ACK",
    "HISTORICALDATAREQUESTS",
    "HISTORICALDATAREQUESTS_ACK",
    "OPTIONSOPERATIONS",
    "OPTIONSOPERATIONS_ACK",
    "CONTRACTOPERATION",
    "CONTRACTOPERATION_ACK",
    "MARKETSCANNERS",
    "MARKETSCANNERS_ACK",
    "FUNDAMENTALS",
    "FUNDAMENTALS_ACK",
    "BULLETINS",
    "BULLETINS_ACK",
    "ACCOUNTOPERATIONS",
    "ACCOUNTOPERATIONS_ACK",
    "ORDEROPERATIONS",
    "ORDEROPERATIONS_ACK",
    "OCASAMPLES",
    "OCASAMPLES_ACK",
    "CONDITIONSAMPLES",
    "CONDITIONSAMPLES_ACK",
    "BRACKETSAMPLES",
    "BRACKETSAMPLES_ACK",
    "HEDGESAMPLES",
    "HEDGESAMPLES_ACK",
    "TESTALGOSAMPLES",
    "TESTALGOSAMPLES_ACK",
    "FAORDERSAMPLES",
    "FAORDERSAMPLES_ACK",
    "FAOPERATIONS",
    "FAOPERATIONS_ACK",
    "DISPLAYGROUPS",
    "DISPLAYGROUPS_ACK",
    "MISCELANEOUS",
    "MISCELANEOUS_ACK",
    "CANCELORDER",
    "CANCELORDER_ACK",
    "FAMILYCODES",
    "FAMILYCODES_ACK",
    "SYMBOLSAMPLES",
    "SYMBOLSAMPLES_ACK",
    "REQMKTDEPTHEXCHANGES",
    "REQMKTDEPTHEXCHANGES_ACK",
    "REQNEWSTICKS",
    "REQNEWSTICKS_ACK",
    "REQSMARTCOMPONENTS",
    "REQSMARTCOMPONENTS_ACK",
    "NEWSPROVIDERS",
    "NEWSPROVIDERS_ACK",
    "REQNEWSARTICLE",
    "REQNEWSARTICLE_ACK",
    "REQHISTORICALNEWS",
    "REQHISTORICALNEWS_ACK",
    "REQHEADTIMESTAMP",
    "REQHEADTIMESTAMP_ACK",
    "REQHISTOGRAMDATA",
    "REQHISTOGRAMDATA_ACK",
    "REROUTECFD",
    "REROUTECFD_ACK",
    "MARKETRULE",
    "MARKETRULE_ACK",
    "PNL",
    "PNL_ACK",
    "PNLSINGLE",
    "PNLSINGLE_ACK",
    "CONTFUT",
    "CONTFUT_ACK",
    "REQHISTORICALTICKS",
    "REQHISTORICALTICKS_ACK",
    "REQTICKBYTICKDATA",
    "REQTICKBYTICKDATA_ACK",
    "WHATIFSAMPLES",
    "WHATIFSAMPLES_ACK" };

/// @brief Brain for the automated trader
///
/// This class handles the connection and callbacks for all operations in the trading bot.
/// Callback functions must be defined in here.
class ClientBrain : public EWrapper
{
public:
    ClientBrain();
    ClientBrain( std::shared_ptr<TradeBase::TBStrategy> );
    ~ClientBrain() = default;
    void setConnectOptions( const std::string& );
    void processMessages();
    bool connect( const char* host, int port, int clientId = 0 );
    void disconnect() const;
    bool isConnected() const;
    void init();
    long getNextConId();
    long getNextOrderId();
    long getNextVectorId();

private:
// Declares virtual methods from EWrapper class
#include "twsapi/EWrapper_prototypes.h"

    std::shared_ptr<TradeBase::TBStrategy> Strategy;
    EReaderOSSignal                        m_osSignal;
    std::shared_ptr<EClientSocket>         p_Client;
    std::shared_ptr<State>                 p_State;
    /// sleep delay time in milliseconds
    time_t                   m_sleepDeadline;
    std::shared_ptr<OrderId> p_OrderId;
    std::shared_ptr<EReader> p_Reader;
    std::shared_ptr<bool>    p_ExtraAuth;
    std::string              m_bboExchange;
    int                      timestamp;
    long                     reqId;
    long                     clientID;
    long                     conId;
    long                     orderId;
    long                     vectorId;
    void                     reqHeadTimestamp();
    long                     getNextReqId();
    void                     reqCurrentTime();

    /* Accounting Operations */
    double cash;
    /// ID for requesting account updates
    unsigned long accReqId;
    /// Unique account identifier, used to request updates
    std::string accountID;
    std::string accountCurrency;
    /// Last time the account object was updated by IB
    time_t accUpdateTime;
    /// Set of positions currently held by the account
    std::set<TradeBase::Position*, TradeBase::positionCompare> positions;
    /// PnL = cash + CostBases - startCash
    double PnL;
    /// UPnL = PositionValues - CostBases
    double UPnL;
    /// Initializes account by subscribing to updates
    void accountInit();
    /// @brief Clone of TradeBase::Brain::update( const Position& )
    ///
    /// Adds or updates a new position to positions set
    void portfolioUpdate( const TradeBase::Position& newPosition );
    /// Cancels account update subscriptions
    void closeSubscriptions();

    /* Data Operations */
    /// Maps a sequential timeline to data points at each time
    TradeBase::TimeMap           TimeLine;
    TradeBase::TimeMap::iterator currentDataPoint;
    TradeBase::TimeStamp         globalStartTime;
    TradeBase::TimeStamp         globalEndTime;
    /// Keeps the starting point of the timer
    std::chrono::_V2::system_clock::time_point start;
    /// Maps a vectorId to a contract
    std::map<long, Contract> conMap;
    /// Contains all historical data requests that have not been answered yet
    /// Up to 50 open Hist requests are allowed at once
    std::set<long> openHistRequests;
    /// Contains all market data lines that are currently active
    /// Up to 100 (including those on the TWS watchlist) can be open at once.
    std::set<long> openDataLines;
    /// Intermediate snapshot structures for holding asynchronous equity data from IB
    std::map<long, SnapHold> snapMap;
    /// Intermediate snapshot structures for holding asynchronous option data from IB
    std::map<long, OptionHold> optionMap;
    /// Intermediate snapshot structures for holding asynchronous future data from IB
    std::map<long, FutureHold> futureMap;
    /// Structures to hold target contracts in
    std::vector<Contract> stockContracts;
    std::vector<Contract> optionContracts;
    std::vector<Contract> futureContracts;

    struct VectorCompare
    {
        using is_transparent = void;
        bool operator()( const std::shared_ptr<TradeBase::DataArray>& lhs, const std::shared_ptr<TradeBase::DataArray>& rhs ) const
        {
            return lhs->vectorId < rhs->vectorId;
        }
        bool operator()( const std::shared_ptr<TradeBase::DataArray>& lhs, long vecId ) const
        {
            return lhs->vectorId < vecId;
        }
        bool operator()( long vecId, const std::shared_ptr<TradeBase::DataArray>& rhs ) const
        {
            return vecId < rhs->vectorId;
        }
    };
    std::set<std::shared_ptr<TradeBase::DataArray>, VectorCompare> DataArrays;

    /// Set of data lines that have been updated since last check
    std::set<long> updatedLines;
    void           dataInit();
    void           harvest( int );
    void           startTimer();
    bool           checkTimer();
    void           updateCandle( TickerId, const Bar& );
    void           updatePrice( TickerId, TradeBase::SnapStruct&, double, int );
    void           updateSize( TickerId, TradeBase::SnapStruct&, int, int );
    void           updatePrice( TickerId, TradeBase::OptionStruct&, double, int );
    void           updateSize( TickerId, TradeBase::OptionStruct&, int, int );
    void           updateOptionGreeks( TickerId, TradeBase::OptionStruct&, double, double, double, double, double, double, double, int );
    bool           updated();
    bool           updated() const;

    void initContractVectors();
    void printCSVs();
    void startLiveData();
    void newHistRequest( Contract&, long, const std::string&, const std::string&, const std::string& );
    void newLiveRequest( Contract&, long );
    void addPoint( long, TradeBase::SnapStruct );
    void addPoint( long, TradeBase::OptionStruct );
    void updateTimeLine( const std::shared_ptr<TradeBase::DataArray>& vec );

    /* Broker Operations */
    /// When servicing callbacks for an orderId, this maps that id to a contract, order pair
    std::map<long, std::pair<Contract, Order>> orderMap;
    /// When servicing execDetails callbacks, this will map the id back to a corresponding contract
    std::map<long, std::pair<Contract, Order>> executionMap;
    /// For comparing contract-order pairs
    struct ConOrdCompare
    {
        using is_transparent = void;
        bool operator()( const std::pair<Contract, Order>& lhs, const std::pair<Contract, Order>& rhs ) const
        {
            return lhs.second.orderId < rhs.second.orderId;
        }
        bool operator()( long lhs, const std::pair<Contract, Order>& rhs ) const
        {
            return lhs < rhs.second.orderId;
        }
        bool operator()( const std::pair<Contract, Order>& lhs, long rhs ) const
        {
            return lhs.second.orderId < rhs;
        }
    };
    /// Contains all orders that have been submitted to IB but have not been accepted
    std::set<std::pair<Contract, Order>, ConOrdCompare> openOrders;
    std::set<std::pair<Contract, Order>, ConOrdCompare> pendingOrders;
    void                                                placeOrder( std::pair<Contract, Order>& );
    void                                                filledOrder( long, const ExecutionFilter& );

    /// Functions I really don't care about but I must have definitions for them according the TWS API rules
    void tickDataOperation();
    void reqHistoricalTicks();
    void reqTickByTickData();
    void marketDepthOperations();
    void realTimeBars();
    void historicalDataRequests();
    void reqMatchingSymbols();
    void reqMktDepthExchanges();
    void marketScanners();
    void reqHistogramData();
    void orderOperations();
    void tickOptionComputation( TickerId TickerId, TickType tickType, double impliedVol, double delta, double optPrice, double pvDividend, double gamma, double vega, double theta, double undPrice );
    void ocaSamples();
    void bracketSample();
    void hedgeSample();
    void rerouteCFDOperations();
    void marketRuleOperations();
    void continuousFuturesOperations();
    void whatIfSamples();
};