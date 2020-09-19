/* Copyright (C) 2019 Interactive Brokers LLC. All rights reserved. This code is
 * subject to the terms and conditions of the IB API Non-Commercial License or
 * the IB API Commercial License, as applicable. */

#pragma once
#ifndef TWS_API_SAMPLES_TESTCPPCLIENT_TESTCPPCLIENT_H
#define TWS_API_SAMPLES_TESTCPPCLIENT_TESTCPPCLIENT_H

#include "EReader.h"
#include "EReaderOSSignal.h"
#include "EWrapper.h"
#include "Position.h"
#include <memory>
#include <set>
#include <vector>

class EClientSocket;

namespace ClientSpace
{

    /// @brief Enumerates all states the Client can be in
    ///
    /// The contributions from the IB API are merely for reference at this point.
    /// States used to be preceded by ST_, changed by TM.
    enum State
    {
        // TradeManager Contribution
        CONNECT,             // attempting a new connection, 0
        CONNECTSUCCESS,      // used just after a successful connection, 1
        CONNECTFAIL,         // a connection attempt has just failed, 2
        DISCONNECTED,        // connection has been broken after initialization
        INIT,                // A new connection has just been established and the trading bot needs
                             // to be initialized
        INITSUCCESS,         // Trading bot is fully and successfully initialized and ready to
                             // trade
        INITFAIL,            // Something went wrong in the initialization function, leads to
                             // immediate disconnect and termination of the program
        ACTIVETRADING,       // The trading bot is actively processing data and placing
                             // trades
        PASSIVETRADING,      // The trading bot is actively processing data but not placing
                             // orders
        ACCOUNTID,           // Waiting to get account ID string. Required to initialize account
        ACCOUNTIDSUCCESS,    // Successfully received account ID string from TWS
        ACCOUNTINIT,         // ClientAccount is being initialized by TWS
        ACCOUNTINITSUCCESS,  // ClientAccount has just been fully and successfully
                             // initialized
        ACCOUNTCLOSE,        // ClientAccount is closing its subscriptions
        ACCOUNTCLOSESUCCESS, // ClientAccount has successfully closed its account
                             // subscriptions
        ACCOUNTCLOSEFAIL,    // An error occurred when trying to close account
                             // subscriptions
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
        // IB API
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
        PING,
        PING_ACK,
        REQHISTORICALTICKS,
        REQHISTORICALTICKS_ACK,
        REQTICKBYTICKDATA,
        REQTICKBYTICKDATA_ACK,
        WHATIFSAMPLES,
        WHATIFSAMPLES_ACK,
        IDLE,
        // TradeManager contribution
        INT
    };

    /// map storing the string version of each state
    extern std::map<int, std::string> StateMap;

    /// @brief Client class from IB
    ///
    /// This class must exist because EWrapper_prototypes.h mandates that all of its
    /// methods be overwritten by its children. Therefore, this class must stay. The
    /// protected shared_ptr's at the end are contributions of TradeManager.
    class Client : public EWrapper
    {
        //! [ewrapperimpl]
    public:
        Client();
        ~Client();

        void setConnectOptions( const std::string& );
        void processMessages();

    public:
        bool connect( const char* host, int port, int clientId = 0 );
        void disconnect() const;
        bool isConnected() const;

    private:
        void pnlOperation();
        void pnlSingleOperation();
        void tickDataOperation();
        void tickOptionComputationOperation();
        void delayedTickDataOperation();
        void marketDepthOperations();
        void realTimeBars();
        void marketDataType();
        void historicalDataRequests();
        void optionsOperations();
        void accountOperations();
        void orderOperations();
        void ocaSamples();
        void conditionSamples();
        void bracketSample();
        void hedgeSample();
        void contractOperations();
        void marketScanners();
        void fundamentals();
        void bulletins();
        void testAlgoSamples();
        void financialAdvisorOrderSamples();
        void financialAdvisorOperations();
        void testDisplayGroups();
        void miscelaneous();
        void reqFamilyCodes();
        void reqMatchingSymbols();
        void reqMktDepthExchanges();
        void reqNewsTicks();
        void reqSmartComponents();
        void reqNewsProviders();
        void reqNewsArticle();
        void reqHistoricalNews();
        void reqHeadTimestamp();
        void reqHistogramData();
        void rerouteCFDOperations();
        void marketRuleOperations();
        void continuousFuturesOperations();
        void reqHistoricalTicks();
        void reqTickByTickData();
        void whatIfSamples();

        void reqCurrentTime();

    public:
#include "EWrapper_prototypes.h"

    private:
        void printContractMsg( const Contract& contract );
        void printContractDetailsMsg( const ContractDetails& contractDetails );
        void printContractDetailsSecIdList( const TagValueListSPtr& secIdList );
        void printBondContractDetailsMsg( const ContractDetails& contractDetails );

    protected:
        EReaderOSSignal                m_osSignal;
        std::shared_ptr<EClientSocket> p_Client;
        std::shared_ptr<State>         p_State;
        /// sleep delay time in milliseconds
        time_t m_sleepDeadline;

        std::shared_ptr<OrderId> p_OrderId;
        std::shared_ptr<EReader> p_Reader;
        std::shared_ptr<bool>    p_ExtraAuth;
        std::string              m_bboExchange;
    };

} // namespace ClientSpace
#endif
