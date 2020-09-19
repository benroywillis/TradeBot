#pragma once
#include "Account.h"
#include "Client.h"

class Position;

class ClientAccount : public ClientSpace::Client, public BTAccount
{
    friend class ClientBrain;

public:
    ClientAccount();
    ClientAccount( double                                     startingCash,
                   const std::shared_ptr<EClientSocket>&      newClient,
                   const std::shared_ptr<ClientSpace::State>& newState );
    ~ClientAccount() = default;

    std::vector<Position*> getPositions() const;
    void                   addClient( std::shared_ptr<EClientSocket> newClient );
    void                   addState( std::shared_ptr<ClientSpace::State> newState );
    /// Initializes account by subscribing to updates
    void init();
    /// Cancels account update subscriptions
    void closeSubscriptions();
    /// ID for requesting account updates
    unsigned long accReqId;
    /// Unique account identifier, used to request updates
    std::string accountID;
    std::string accountCurrency;
    /// Last time the account object was updated by IB
    time_t accUpdateTime;

private:
    /// Shows that the account is ready for trading
    bool valid;
};