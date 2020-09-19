#pragma once
#include "Client.h"
#include "Data.h"
#include "DataStruct.h"
#include "DataTypes.h"

class DataArray;

struct SnapHold
{
    SnapStruct bidAsk;
    SnapStruct lastTrade;
};

struct OptionHold
{
    OptionStruct bidAsk;
    OptionStruct lastTrade;
};

class ClientData : public ClientSpace::Client, public BTData
{
    friend class ClientBrain;

public:
    ClientData( std::vector<BTIndicator*>& );
    ClientData( const std::shared_ptr<EClientSocket>&,
                const std::shared_ptr<ClientSpace::State>& );
    ClientData();
    void addClient( std::shared_ptr<EClientSocket> );
    void addState( std::shared_ptr<ClientSpace::State> );
    void init();
    void harvest( int );
    void startTimer();
    bool checkTimer();
    void updateCandle( TickerId, const Bar& );
    void updatePrice( TickerId, SnapStruct&, double, int );
    void updateSize( TickerId, SnapStruct&, int, int );
    void updatePrice( TickerId, OptionStruct&, double, int );
    void updateSize( TickerId, OptionStruct&, int, int );
    void updateOptionGreeks( TickerId, OptionStruct&, double, double, double,
                             double, double, double, double, int );
    bool updated();
    bool updated() const;

private:
    void initContractVectors();
    void startLiveData();
    void newHistRequest( Contract&, long, const std::string&,
                         const std::string&, const std::string& );
    void newLiveRequest( Contract&, long );
    void addPoint( long, SnapStruct );
    void addPoint( long, OptionStruct );
    void updateTimeLine( const std::shared_ptr<DataArray>& vec );

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

    /// Intermediate snapshot structures for holding asynchronous data from IB
    std::map<long, SnapHold> snapMap;

    /// Intermediate snapshot structures for holding asynchronous data from IB
    std::map<long, OptionHold> optionMap;

    /// Structures to hold target contracts in
    std::vector<Contract> stockContracts;
    std::vector<Contract> optionContracts;

    /// Set of data lines that have been updated since last check
    std::set<long> updatedLines;

    /// Flag showing that this object is ready for trading
    bool valid;
};